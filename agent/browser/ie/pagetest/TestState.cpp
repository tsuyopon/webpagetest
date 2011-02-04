/*
Copyright (c) 2005-2007, AOL, LLC.

All rights reserved.

Redistribution and use in source and binary forms, with or without modification, 
are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice, 
		this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright notice, 
		this list of conditions and the following disclaimer in the documentation 
		and/or other materials provided with the distribution.
    * Neither the name of the company nor the names of its contributors may be 
		used to endorse or promote products derived from this software without 
		specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "StdAfx.h"
#include "TestState.h"
#include <atlutil.h>
#include <Mmsystem.h>
#include "Psapi.h"

// various timeouts that control when we call a page done
#define ACTIVITY_TIMEOUT 2000
#define REQUEST_ACTIVITY_TIMEOUT 30000
#define FORCE_ACTIVITY_TIMEOUT 240000
#define DOC_TIMEOUT 1000
#define AFT_TIMEOUT 240000

CTestState::CTestState(void):
	hTimer(0)
	,lastBytes(0)
	,lastProcessTime(0)
	,lastTime(0)
	,windowUpdated(true)
	,hBrowserWnd(NULL)
	,imageCount(0)
	,lastImageTime(0)
	,lastRealTime(0)
	,cacheCleared(false)
{
}

CTestState::~CTestState(void)
{
}

/*-----------------------------------------------------------------------------
-----------------------------------------------------------------------------*/
void CTestState::Reset(void)
{
	__super::Reset();
	
	EnterCriticalSection(&cs);
	currentState = READYSTATE_UNINITIALIZED;
	painted = false;
	windowUpdated = true;
	LeaveCriticalSection(&cs);
}

/*-----------------------------------------------------------------------------
	Do all of the startup checks and evaluations
-----------------------------------------------------------------------------*/
void CTestState::DoStartup(CString& szUrl, bool initializeDoc)
{
	USES_CONVERSION;
	CString msg;
	
	if( !active && available )
	{
		msg.Format(_T("[Pagetest] *** DoStartup() - '%s'\n"), (LPCTSTR)szUrl);
		OutputDebugString(msg);
		
		bool ok = true;

		CheckABM();

		domElementId.Empty();
		domRequest.Empty();
		domRequestType = END;
		endRequest.Empty();

		if( interactive )
		{
			checkOpt = true;

			if( runningScript )
			{
				CString szEventName = szUrl;				// default this to the url for right now
				if( !script_eventName.IsEmpty() )
					szEventName = script_eventName;
				domElementId = script_domElement;
				domRequest = script_domRequest;
				domRequestType = script_domRequestType;
				endRequest = script_endRequest;
				
				if( script_timeout != -1 )
					timeout = script_timeout;

				if( !szEventName.IsEmpty() && szEventName == somEventName )
					ok = false;
				else
					somEventName = szEventName;
			}
		}
		else
		{
			// load the automation settings from the registry
			CRegKey key;		
			if( key.Open(HKEY_CURRENT_USER, _T("Software\\America Online\\SOM"), KEY_READ | KEY_WRITE) == ERROR_SUCCESS )
			{
				CString szEventName = szUrl;				// default this to the url for right now

				TCHAR buff[1024];
				ULONG len = sizeof(buff) / sizeof(TCHAR);

				if( key.QueryStringValue(_T("EventName"), buff, &len) == ERROR_SUCCESS )
					szEventName = buff;

				if( runningScript )
				{
					if( script_active )
					{
						if( !script_eventName.IsEmpty() )
						{
							if( szEventName.IsEmpty() )
								szEventName = script_eventName;
							else
							{
								if( !szEventName.Replace(_T("%STEP%"), (LPCTSTR)script_eventName) )
									szEventName += CString(_T("_")) + script_eventName;
							}
						}
						domElementId = script_domElement;
						domRequest = script_domRequest;
						domRequestType = script_domRequestType;
						endRequest = script_endRequest;
					}
					else
						ok = false;
				}
				else
				{
					len = sizeof(buff) / sizeof(TCHAR);
					if( key.QueryStringValue(_T("DOM Element ID"), buff, &len) == ERROR_SUCCESS )
						if( lstrlen(buff) )
							domElementId = buff;
					key.DeleteValue(_T("DOM Element ID"));
				}

				len = sizeof(buff) / sizeof(TCHAR);
				logFile.Empty();
				if( key.QueryStringValue(_T("IEWatchLog"), buff, &len) == ERROR_SUCCESS )
					logFile = buff;
					
				len = sizeof(buff) / sizeof(TCHAR);
				linksFile.Empty();
				if( key.QueryStringValue(_T("Links File"), buff, &len) == ERROR_SUCCESS )
					linksFile = buff;
				key.DeleteValue(_T("Links File"));

				len = sizeof(buff) / sizeof(TCHAR);
				s404File.Empty();
				if( key.QueryStringValue(_T("404 File"), buff, &len) == ERROR_SUCCESS )
					s404File = buff;
				key.DeleteValue(_T("404 File"));

				len = sizeof(buff) / sizeof(TCHAR);
				htmlFile.Empty();
				if( key.QueryStringValue(_T("HTML File"), buff, &len) == ERROR_SUCCESS )
					htmlFile = buff;
				key.DeleteValue(_T("HTML File"));

				len = sizeof(buff) / sizeof(TCHAR);
				cookiesFile.Empty();
				if( key.QueryStringValue(_T("Cookies File"), buff, &len) == ERROR_SUCCESS )
					cookiesFile = buff;
				key.DeleteValue(_T("Cookies File"));

				// if we're running a script, the block list will come from the script
				if( !runningScript )
				{
					len = sizeof(buff) / sizeof(TCHAR);
					blockRequests.RemoveAll();
					if( key.QueryStringValue(_T("Block"), buff, &len) == ERROR_SUCCESS )
					{
						CString block = buff;
						int pos = 0;
						CString token = block.Tokenize(_T(" "), pos);
						while( pos >= 0 )
						{
							token.Trim();
							blockRequests.AddTail(token);
							token = block.Tokenize(_T(" "), pos);
						}
					}
					key.DeleteValue(_T("Block"));
				}

				len = sizeof(buff) / sizeof(TCHAR);
				basicAuth.Empty();
				if( key.QueryStringValue(_T("Basic Auth"), buff, &len) == ERROR_SUCCESS )
				{
					basicAuth = buff;
					script_basicAuth = buff;
				}
				key.DeleteValue(_T("Basic Auth"));

				if( ok )
				{
					if( runningScript )
						logUrl[0]=0;
					else
					{
						len = _countof(logUrl);
						key.QueryStringValue(_T("URL"), logUrl, &len);
					}
					key.QueryDWORDValue(_T("LabID"), labID);
					key.QueryDWORDValue(_T("DialerID"), dialerID);
					key.QueryDWORDValue(_T("ConnectionType"), connectionType);
					key.QueryDWORDValue(_T("Cached"), cached);
					experimental = 0;
					key.QueryDWORDValue(_T("Experimental"), experimental);
					includeObjectData = 1;
					key.QueryDWORDValue(_T("Include Object Data"), includeObjectData);
					saveEverything = 0;
					key.QueryDWORDValue(_T("Save Everything"), saveEverything);
					captureVideo = 0;
					key.QueryDWORDValue(_T("Capture Video"), captureVideo);
					screenShotErrors = 0;
					key.QueryDWORDValue(_T("Screen Shot Errors"), screenShotErrors);
					checkOpt = 1;
					key.QueryDWORDValue(_T("Check Optimizations"), checkOpt);
					ignoreSSL = 0;
					key.QueryDWORDValue(_T("ignoreSSL"), ignoreSSL);
					DWORD useBitBlt = 0;
					key.QueryDWORDValue(_T("useBitBlt"), useBitBlt);
					if( useBitBlt )
						forceBlit = true;
          aft = 0;
					key.QueryDWORDValue(_T("AFT"), aft);

					len = sizeof(buff) / sizeof(TCHAR);
					customHost.Empty();
					if( key.QueryStringValue(_T("Host"), buff, &len) == ERROR_SUCCESS )
						customHost = buff;

					if( !runningScript )
					{
						len = _countof(descriptor);
						key.QueryStringValue(_T("Descriptor"), descriptor, &len);

						// delete values that shouldn't be re-used
						key.DeleteValue(_T("Descriptor"));
						key.DeleteValue(_T("URL"));
						key.DeleteValue(_T("Cached"));
						key.DeleteValue(_T("Save Everything"));
						key.DeleteValue(_T("ignoreSSL"));
						key.DeleteValue(_T("Host"));
					}
						
					// make sure the event name has changed
					// this is to prevent a page with navigate script on it 
					// from adding test entries to the log file
					if( !szEventName.IsEmpty() && szEventName == somEventName )
					{
						msg.Format(_T("[Pagetest] *** Ingoring event, event name has not changed - '%s'\n"), (LPCTSTR)somEventName);
						OutputDebugString(msg);
						ok = false;
					}
					else
						somEventName = szEventName;
				}

				key.Close();
			}
			else
				ok = false;

			// load iewatch settings
			if( ok )
			{
				bindAddr = 0;
				if( key.Open(HKEY_CURRENT_USER, _T("SOFTWARE\\AOL\\ieWatch"), KEY_READ) == ERROR_SUCCESS )
				{
					if( runningScript && script_timeout != -1 )
						timeout = script_timeout;
					else
						key.QueryDWORDValue(_T("Timeout"), timeout);
					
          TCHAR addr[100];
          addr[0] = 0;
					ULONG len = _countof(addr);
					if( key.QueryStringValue(_T("Use Address"), addr, &len) == ERROR_SUCCESS && lstrlen(addr))
					{
						CString buff;
						buff.Format(_T("[Pagetest] - Binding to local address %s\n"), addr);
						OutputDebugString(buff);
						bindAddr = inet_addr(CT2A(addr));
					}
						
					key.Close();
				}		
				
				if( key.Open(HKEY_LOCAL_MACHINE, _T("SOFTWARE\\AOL\\ieWatch"), KEY_READ) == ERROR_SUCCESS )
				{
					key.QueryDWORDValue(_T("Include Header"), includeHeader);
					key.Close();
				}		
			}
		}

		// clear the cache if necessary (extra precaution)
		if( ok && !cached && !cacheCleared )
		{
			GROUPID id;
			HANDLE hGroup = FindFirstUrlCacheGroup(0, CACHEGROUP_SEARCH_ALL, 0,0, &id, 0);
			if( hGroup )
			{
				do
				{
					DeleteUrlCacheGroup(id, CACHEGROUP_FLAG_FLUSHURL_ONDELETE, 0);
				}while(FindNextUrlCacheGroup(hGroup, &id,0));

				FindCloseUrlCache(hGroup);
			}

			// just use a huge buffer big enough to hold more than it will ever need to avoid constantly re-allocating it
			DWORD dwSize = 102400;
			INTERNET_CACHE_ENTRY_INFO * info = (INTERNET_CACHE_ENTRY_INFO *)malloc(dwSize);
			if( info )
			{
				DWORD len = dwSize / sizeof(TCHAR);
				HANDLE hEntry = FindFirstUrlCacheEntry(NULL, info, &len);
				if( hEntry )
				{
					do
					{
						DeleteUrlCacheEntry(info->lpszSourceUrlName);
						len = dwSize / sizeof(TCHAR);
					}
					while(FindNextUrlCacheEntry(hEntry, info, &len));
				}

				free(info);
			}

			cacheCleared = true;
		}
		
		if( ok )
		{
			// check for any machine-wide overrides
			CRegKey keyMachine;		
			if( keyMachine.Open(HKEY_LOCAL_MACHINE, _T("Software\\America Online\\Pagetest"), KEY_READ) == ERROR_SUCCESS )
			{
				DWORD val = checkOpt;
				if( ERROR_SUCCESS == keyMachine.QueryDWORDValue(_T("Check Optimizations"), val) )
					checkOpt = val;
				keyMachine.Close();
			}

			// parse any test options that came in on the url
			ParseTestOptions();
			
			msg.Format(_T("[Pagetest] *** DoStartup() - Starting measurement - '%s'\n"), (LPCTSTR)somEventName);
			OutputDebugString(msg);

			// create the dialog if we need to
			Create();

			// delete any old data
			Reset();

			// track the document that everything belongs to
			if( initializeDoc )
			{
				EnterCriticalSection(&cs);
				currentDoc = nextDoc;
				nextDoc++;
				LeaveCriticalSection(&cs);
			}
			
			EnterCriticalSection(&cs);
			active = true;
			available = false;
      if( aft )
        capturingAFT = true;
			reportSt = NONE;
			
			// collect the starting TCP stats
			GetTcpStatistics(&tcpStatsStart);

			// keep the activity tracking up to date
			QueryPerformanceCounter((LARGE_INTEGER *)&lastRequest);
			lastActivity = lastRequest;

			startTime = CTime::GetCurrentTime();
			url = szUrl;
			
			LeaveCriticalSection(&cs);

			StartTimer(1, 100);
		}
	}
	else
	{
		msg.Format(_T("[Pagetest] *** DoStartup() - event dropped because we are already active or not available - '%s'\n"), (LPCTSTR)szUrl);
		OutputDebugString(msg);
	}
}

/*-----------------------------------------------------------------------------
	See if the test is complete
-----------------------------------------------------------------------------*/
void CTestState::CheckComplete()
{
	ATLTRACE(_T("[Pagetest] - Checking to see if the test is complete\n"));

	if( active || capturingAFT )
	{
		CString buff;
		bool expired = false;
    bool done = false;

	  __int64 now;
	  QueryPerformanceCounter((LARGE_INTEGER *)&now);

    // only do the request checking if we're actually active
    if( active )
    {
      EnterCriticalSection(&cs);
  		
		  // has our timeout expired?
		  if( timeout && start )
		  {
			  DWORD elapsed =  (DWORD)((now - start) / freq);
			  if( elapsed > timeout )
			  {
				  buff.Format(_T("[Pagetest] - Test timed out (timout set to %d sec)\n"), timeout);
				  OutputDebugString(buff);
  				
				  expired = true;
			  }
			  else
			  {
				  ATLTRACE(_T("[Pagetest] - Elapsed test time: %d sec\n"), elapsed);
			  }
		  }
		  else
		  {
			  ATLTRACE(_T("[Pagetest] - Start time not logged yet\n"));
		  }

		  LeaveCriticalSection(&cs);

		  // see if the DOM element we're interested in appeared yet
		  CheckDOM();

		  // only exit if there isn't an outstanding doc or request
		  if( (lastRequest && !currentDoc) || expired || forceDone || errorCode )
		  {
			  done = forceDone || errorCode != 0;
  			
			  if( !done )
			  {
				  // count the number of open wininet requests
				  EnterCriticalSection(&cs);
				  openRequests = 0;
				  POSITION pos = winInetRequestList.GetHeadPosition();
				  while( pos )
				  {
					  CWinInetRequest * r = winInetRequestList.GetNext(pos);
					  if( r && r->valid && !r->closed )
						  openRequests++;
				  }
				  LeaveCriticalSection(&cs);


				  ATLTRACE(_T("[Pagetest] - %d openRequests"), openRequests);

				  // did the DOM element arrive yet (if we're looking for one?)
				  if( (domElement || (domElementId.IsEmpty() && domRequest.IsEmpty())) && requiredRequests.IsEmpty() && !script_waitForJSDone )
				  {
					  // see if we are done (different logic if we're in abm mode or not)
					  if( abm )
					  {
              DWORD elapsed = now > lastActivity && lastActivity ? (DWORD)((now - lastActivity ) / (freq / 1000)) : 0;
              DWORD elapsedRequest = now > lastRequest && lastRequest ? (DWORD)((now - lastRequest ) / (freq / 1000)) : 0;
						  if ( (!openRequests && elapsed > ACTIVITY_TIMEOUT) ||					// no open requests and it's been longer than 2 seconds since the last request
							   (!openRequests && elapsedRequest > REQUEST_ACTIVITY_TIMEOUT) ||	// no open requests and it's been longer than 30 seconds since the last traffic on the wire
							   (openRequests && elapsedRequest > FORCE_ACTIVITY_TIMEOUT) )	// open requests but it's been longer than 60 seconds since the last one (edge case) that touched the wire
						  {
							  done = true;
							  OutputDebugString(_T("[Pagetest] ***** Measured as Web 2.0\n"));
						  }
					  }
					  else
					  {
						  if( lastDoc )	// make sure we actually measured a document - shouldn't be possible to not be set but just to be safe
						  {
							  DWORD elapsed = (DWORD)((now - lastDoc) / (freq / 1000));
							  if( elapsed > DOC_TIMEOUT )
							  {
								  done = true;
								  OutputDebugString(_T("[Pagetest] ***** Measured as Web 1.0\n"));
							  }
						  }
					  }
				  }
			  }
			  else
			  {
				  buff.Format(_T("[Pagetest] - Force exit. Error code = %d (0x%08X)\n"), errorCode, errorCode);
				  OutputDebugString(buff);
			  }
      }
    }
    else
    {
      // check to see if we are done capturing AFT video
		  DWORD elapsed = (DWORD)((now - end) / (freq / 1000));
		  if( elapsed > AFT_TIMEOUT )
        done = true;
    }
			
		if ( expired || done )
		{
      if( active && capturingAFT )
      {
        OutputDebugString(_T("[Pagetest] ***************** Data collection complete, continuing to capture video for AFT\n"));

        // turn off regular data capture but keep capturing video
        active = false;
		    if( !end || abm )
			    end = lastRequest;

		    // get a screen shot of the fully loaded page
		    if( saveEverything && !imgFullyLoaded.IsValid())
			    GrabScreenShot(imgFullyLoaded);
      }
      else
      {
			  CString buff;
			  buff.Format(_T("[Pagetest] ***** Page Done\n")
						  _T("[Pagetest]          Document ended: %0.3f sec\n")
						  _T("[Pagetest]          Last Activity:  %0.3f sec\n")
						  _T("[Pagetest]          DOM Element:  %0.3f sec\n"),
						  !endDoc ? 0.0 : (double)(endDoc-start) / (double)freq,
						  !lastRequest ? 0.0 : (double)(lastRequest-start) / (double)freq,
						  !domElement ? 0.0 : (double)(domElement-start) / (double)freq);
			  OutputDebugString(buff);

        // see if we are combining multiple script steps (in which case we need to start again)
        if( runningScript && script_combineSteps && script_combineSteps != 1 && !script.IsEmpty() )
        {
          if( script_combineSteps > 1 )
            script_combineSteps--;

          // do some basic resetting
          end = 0;
          lastRequest = 0;
          lastActivity = 0;
          endDoc = 0;

          ContinueScript(false);
        }
        else
        {
			    // keep track of the end time in case there wasn't a document
			    if( !end || abm )
				    end = lastRequest;

			    // put some text on the browser window to indicate we're done
			    double sec = (start && end > start) ? (double)(end - start) / (double)freq: 0;
			    if( !expired )
				    reportSt = TIMER;
			    else
				    reportSt = QUIT_NOEND;

			    RepaintWaterfall();

			    // kill the background timer
			    if( hTimer )
			    {
				    DeleteTimerQueueTimer(NULL, hTimer, NULL);
				    hTimer = 0;
				    timeEndPeriod(1);
			    }

			    // get a screen shot of the fully loaded page
			    if( saveEverything && !imgFullyLoaded.IsValid())
				    GrabScreenShot(imgFullyLoaded);

			    // write out any results (this will also kill the timer)
			    FlushResults();
        }
      }
		}
	}
}

/*-----------------------------------------------------------------------------
	See if the browser's readystate has changed
-----------------------------------------------------------------------------*/
void CTestState::CheckReadyState(void)
{
	// figure out the old state (first non-complete browser window)
	EnterCriticalSection(&cs);
	READYSTATE oldState = READYSTATE_COMPLETE;
	POSITION pos = browsers.GetHeadPosition();
	while( pos && oldState == READYSTATE_COMPLETE )
	{
		CBrowserTracker tracker = browsers.GetNext(pos);
		if( tracker.state != READYSTATE_COMPLETE )
			oldState = tracker.state;
	}
	
	// update the state for all browsers in this thread
	CAtlList<CComPtr<IWebBrowser2>> browsers2;
	pos = browsers.GetHeadPosition();
	while( pos )
	{
		POSITION oldPos = pos;
		CBrowserTracker tracker = browsers.GetNext(pos);
		if(tracker.browser && tracker.threadId == GetCurrentThreadId())
			tracker.browser->get_ReadyState(&(browsers.GetAt(oldPos).state));
	}

	// see what the new state is
	READYSTATE newState = READYSTATE_COMPLETE;
	pos = browsers.GetHeadPosition();
	while( pos && newState == READYSTATE_COMPLETE )
	{
		CBrowserTracker tracker = browsers.GetNext(pos);
		if( tracker.state != READYSTATE_COMPLETE )
			newState = tracker.state;
	}
	LeaveCriticalSection(&cs);

	if( newState != oldState )
	{
		currentState = newState;
		CString state;
		switch(currentState)
		{
			case READYSTATE_UNINITIALIZED: state = "Uninitialized"; break;
			case READYSTATE_LOADING: state = "Loading"; break;
			case READYSTATE_LOADED: state = "Loaded"; break;
			case READYSTATE_INTERACTIVE: state = "Interactive"; break;
			case READYSTATE_COMPLETE: 
					{
						state = "Complete"; 
						
						// force a DocumentComplete in case we never got notified
						if( active && currentDoc )
							DocumentComplete(url);
					}
					break;
			default: state = "Unknown"; break;
		}
		
		CString buff;
		buff.Format(_T("[Pagetest] * Browser ReadyState changed to %s\n"), (LPCTSTR)state);
		OutputDebugString(buff);
	}
}


/*-----------------------------------------------------------------------------
	Check to see if a specific DOM element we're looking for has been loaded yet
-----------------------------------------------------------------------------*/
void CTestState::CheckDOM(void)
{
	// don't bother if we already found it
	if(!domElementId.IsEmpty() && !domElement && startRender)
	{
		if( FindDomElementByAttribute(domElementId) )
		{
			QueryPerformanceCounter((LARGE_INTEGER *)&domElement);
			lastRequest = lastActivity = domElement;
		
			CString buff;
			buff.Format(_T("[Pagetest] * DOM Element ID '%s' appeared\n"), (LPCTSTR)domElementId);
			OutputDebugString(buff);
			
			if( saveEverything )
				GrabScreenShot(imgDOMElement);
		}
	}
}

/*-----------------------------------------------------------------------------
	Check to see if the UI started rendering yet
-----------------------------------------------------------------------------*/
void CTestState::CheckRender()
{
	if( !startRender )
	{
		// only check the last browser window opened
		CBrowserTracker tracker = browsers.GetTail();
		if( tracker.threadId == GetCurrentThreadId() && tracker.browser )
		{
			READYSTATE rs;
			if( SUCCEEDED(tracker.browser->get_ReadyState(&rs)) && (rs == READYSTATE_INTERACTIVE || rs == READYSTATE_COMPLETE))
			{
				CComPtr<IDispatch> spDoc;
				if( SUCCEEDED(tracker.browser->get_Document(&spDoc)) && spDoc )
				{
					CComQIPtr<IHTMLDocument2> doc = spDoc;
					CComQIPtr<IHTMLDocument3> doc3 = spDoc;
					CComPtr<IHTMLElement> body;
					if( doc && SUCCEEDED(doc->get_body(&body)) )
					{
						CComQIPtr<IHTMLElement2> body2 = body;
						if( body2 )
						{
							long width = 0;
							long height = 0;
							body2->get_scrollWidth(&width);
							body2->get_scrollHeight(&height);
							
							CComPtr<IHTMLElement> docElement;
							if( doc3 && SUCCEEDED(doc3->get_documentElement(&docElement)) )
							{
								CComQIPtr<IHTMLElement2> docElement2 = docElement;
								if( docElement2 )
								{
									long w2 = 0;
									long h2 = 0;
									docElement2->get_scrollWidth(&w2);
									docElement2->get_scrollHeight(&h2);
									
									width += w2;
									height += h2;
								}
							}
							
							if( width && height )
							{
								QueryPerformanceCounter((LARGE_INTEGER *)&startRender);
								CString buff;
								buff.Format(_T("[Pagetest] * Render Start : %dx%d\n"), width, height);
								OutputDebugString(buff);
							}
						}
					}
				}
			}
		}
	}
}

/*-----------------------------------------------------------------------------
	Check to see if anything was drawn to the screen
-----------------------------------------------------------------------------*/
void CTestState::CheckWindowPainted(HWND hWnd)
{
	if( active && !painted && hBrowserWnd && ::IsWindow(hBrowserWnd) )
	{
		__int64 now;
		QueryPerformanceCounter((LARGE_INTEGER *)&now);

		CString display;
		int i = 0;
		while( i >= 0 && display.IsEmpty() )
		{
			DISPLAY_DEVICE device;
			device.cb = sizeof(device);
			if( EnumDisplayDevices(NULL, i, &device, 0) )
			{
				if( device.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE )
					display = device.DeviceName;
				i++;
			}
			else
				i = -1;
		}
		// grab a screen shot of the window
		HDC hDisplay = NULL;
		HDC hSrc = NULL;
    if( (forceBlit || captureVideo || display.IsEmpty()) && hBrowserWnd && ::IsWindow(hBrowserWnd) )
			hSrc = ::GetDC(hBrowserWnd);
    else if( !display.IsEmpty() )
			hDisplay = hSrc = CreateDC(display, display, NULL, NULL);
		if( hSrc )
		{
			HDC hDC = CreateCompatibleDC(hSrc);
			if( hDC )
			{
				CRect rect;
				if( ::GetWindowRect(hBrowserWnd, rect) )
				{
					int w = rect.Width();
					int h = rect.Height();

					// create an in-memory DIB
					BITMAPINFO bi;
					memset(&bi, 0, sizeof(bi));
					bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
					bi.bmiHeader.biWidth = w;
					bi.bmiHeader.biHeight = h;
					bi.bmiHeader.biPlanes = 1;
					bi.bmiHeader.biBitCount = 24;
					bi.bmiHeader.biCompression = BI_RGB;
					BYTE *pbBitmap;
					HBITMAP hBitmap = CreateDIBSection(hDC, &bi, DIB_RGB_COLORS,(void**)&pbBitmap, NULL, 0);
					if (hBitmap)
					{
						int margin = 5;		// skip the drop border
						int rMargin = 30;	// skip the scroll bar
						
						HBITMAP hBitmapOld = (HBITMAP)SelectObject(hDC, hBitmap);
						
						BOOL ok = FALSE;
						if( forceBlit || captureVideo )
							ok = BitBlt(hDC, 0, 0, w, h, hSrc, 0, 0, SRCCOPY);
						else
							ok = ::PrintWindow(hBrowserWnd, hDC, 0);

						if( ok )
						{
							// scan for any non-white pixels
							bool found = false;
							DWORD * row = (DWORD *)pbBitmap;
							DWORD rowCount = (w * 3) / sizeof(DWORD);
							DWORD rowLen = rowCount;
							if( (w * 3) % sizeof(DWORD) )
								rowLen++;
							
							// add the top and bottom margins
							row  += margin * rowLen;
							h -= margin * 2;
							
							// go through one row at a time
							while( !found && h > 0 )
							{
								DWORD * p = row + ((margin * 3) / sizeof(DWORD));
								DWORD count = rowCount - (((margin + rMargin) * 3) / sizeof(DWORD));

								while( !found && count > 0 )
								{
									if( *p ^ 0xFFFFFFFF )
										found = true;

									count--;
									p++;
								}
								
								// on to the next row
								h--;
								row += rowLen;
							}
							
							if( found )
							{
								painted = true;
								windowUpdated = true;
								startRender = now;

								OutputDebugString(_T("[Pagetest] * Render Start (Painted)"));

								// Save the screen shot (just the window is fine so we don't double-grab)
								if( saveEverything )
									imgStartRender.CreateFromHBITMAP(hBitmap);
							}
						}
							
						SelectObject(hDC, hBitmapOld);
						DeleteObject(hBitmap);
					}
				}
				DeleteDC(hDC);
			}
			
			if( hDisplay )
				DeleteDC(hDisplay);
			else
				::ReleaseDC(hBrowserWnd, hSrc);
		}
	}
}

/*-----------------------------------------------------------------------------
	Parse the test options string
-----------------------------------------------------------------------------*/
void CTestState::ParseTestOptions()
{
	TCHAR buff[4096];
	if( !testOptions.IsEmpty() )
	{
		int pos = 0;
		do
		{
			// commands are separated by & just like query parameters
			CString token = testOptions.Tokenize(_T("&"), pos);
			if( token.GetLength() )
			{
				int index = token.Find(_T('='));
				if( index > 0 )
				{
					CString command = token.Left(index).Trim();
					if( command.GetLength() )
					{
						// any values need to be escaped since it  is passed in on the url so un-escape it
						CString tmp = token.Mid(index + 1);
						DWORD len;
						if( AtlUnescapeUrl((LPCTSTR)tmp, buff, &len, _countof(buff)) )
						{
							CString value = buff;
							value = value.Trim();
						
							// now handle the actual command
							if( !command.CompareNoCase(_T("ptBlock")) )
							{
								// block the specified request
								blockRequests.AddTail(value);
							}
							if( !command.CompareNoCase(_T("ptAds")) )
							{
								// block aol-specific ad calls
								if( !value.CompareNoCase(_T("none")) || !value.CompareNoCase(_T("block")) )
								{
									blockRequests.AddTail(_T("adsWrapper.js"));
									blockRequests.AddTail(_T("adsWrapperAT.js"));
									blockRequests.AddTail(_T("adsonar.js"));
									blockRequests.AddTail(_T("sponsored_links1.js"));
									blockRequests.AddTail(_T("switcher.dmn.aol.com"));
								}
							}
						}
					}
				}
			}
		}while( pos >= 0 );
	}

	// see if the DOM element was really a DOM request in hiding
	if( domElementId.GetLength() )
	{
		int pos = 0;
		CString action = domElementId.Tokenize(_T("="), pos).Trim();
		if( pos != -1 )
		{
			CString val = domElementId.Tokenize(_T("="), pos).Trim();
			if( val.GetLength() )
			{
				if( !action.CompareNoCase(_T("RequestEnd")) )
				{
					domRequest = val;
					domRequestType = END;
					domElementId.Empty();
				}
				else if( !action.CompareNoCase(_T("RequestTTFB")) )
				{
					domRequest = val;
					domRequestType = TTFB;
					domElementId.Empty();
				}
				else if( !action.CompareNoCase(_T("RequestStart")) )
				{
					domRequest = val;
					domRequestType = START;
					domElementId.Empty();
				}
			}
		}
	}
}

VOID CALLBACK BackgroundTimer(PVOID lpParameter, BOOLEAN TimerOrWaitFired)
{
	if( lpParameter )
		((CTestState *)lpParameter)->BackgroundTimer();
}

/*-----------------------------------------------------------------------------
	Measurement is starting, kick off the background stuff
-----------------------------------------------------------------------------*/
void CTestState::StartMeasuring(void)
{
	// create thee background timer to fire every 100ms
	if( !hTimer && saveEverything && (!runningScript || script_logData) )
	{
		lastBytes = 0;
		lastProcessTime = 0;
		lastTime = 0;
		imageCount = 0;
		lastImageTime = 0;
		lastRealTime = 0;
		windowUpdated = true;	// force an initial screen shot

		// get the handle for the browser window we're going too be watching (and screen shotting)
		hBrowserWnd = hMainWindow;
		if( !hBrowserWnd )
		{
			CBrowserTracker tracker = browsers.GetTail();
			if( tracker.browser )
				tracker.browser->get_HWND((SHANDLE_PTR *)&hBrowserWnd);
		}

		// move the window to the top if we are capturing video frames
		if( (forceBlit || captureVideo) && hBrowserWnd && ::IsWindow(hBrowserWnd) )
		{
			::SetWindowPos(hBrowserWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE);
			::UpdateWindow(hBrowserWnd);
		}

		// now find just the browser control
		FindBrowserControl(hBrowserWnd, hBrowserWnd);

		timeBeginPeriod(1);
		CreateTimerQueueTimer(&hTimer, NULL, ::BackgroundTimer, this, 100, 100, WT_EXECUTEDEFAULT);

		// Force a grab/stats capture now
		BackgroundTimer();
	}
}

/*-----------------------------------------------------------------------------
	Do the 100ms periodic checking
-----------------------------------------------------------------------------*/
void CTestState::BackgroundTimer(void)
{
	// queue up a message in case we're having timer problems
	CheckStuff();

	// timer will only be running while we're active
	EnterCriticalSection(&csBackground);
	const DWORD imageIncrements = 20;	// allow for X screen shots at each increment

	__int64 now;
	QueryPerformanceCounter((LARGE_INTEGER *)&now);
	if( active || capturingAFT )
	{
		CProgressData data;

		DWORD ms = 0;
		if( start && now > start )
			ms = (DWORD)((now - start) / msFreq);

		// round to the closest 100ms
		data.ms = (ms / 100) * 100;
		if( ms % 100 >= 50 )
			data.ms += 100;

		// don't re-do everything if we get a burst of timer callbacks
		if( data.ms != lastTime || !lastTime )
		{
			DWORD msElapsed = 0;
			if( data.ms > lastTime )
				msElapsed = data.ms - lastTime;

			double elapsed = 0;
			if( now > lastRealTime && lastRealTime)
				elapsed = (double)(now - lastRealTime) / (double)freq;
			lastRealTime = now;

			// figure out the bandwidth
			if( lastBytes )
				data.bpsIn = lastBytes * 800;	// * 100 for the interval and * 8 for Bytes->bits
			lastBytes = bwBytesIn;
			bwBytesIn = 0;

			// calculate CPU utilization
			FILETIME create, ex, kernel, user;
			if( GetProcessTimes(GetCurrentProcess(), &create, &ex, &kernel, &user) )
			{
				ULARGE_INTEGER k, u;
				k.LowPart = kernel.dwLowDateTime;
				k.HighPart = kernel.dwHighDateTime;
				u.LowPart = user.dwLowDateTime;
				u.HighPart = user.dwHighDateTime;
				unsigned __int64 cpuTime = k.QuadPart + u.QuadPart;
				if( lastProcessTime && cpuTime >= lastProcessTime && elapsed > 0.0)
				{
					double delta = (double)(cpuTime - lastProcessTime) / (double)10000000; // convert it to seconds of CPU time
					data.cpu = min((double)delta / elapsed, 1.0) * 100.0;
				}
				lastProcessTime = cpuTime;
			}

			// get the memory use (working set - task-manager style)
			PROCESS_MEMORY_COUNTERS mem;
			mem.cb = sizeof(mem);
			if( GetProcessMemoryInfo(GetCurrentProcess(), &mem, sizeof(mem)) )
				data.mem = mem.WorkingSetSize / 1024;

			// interpolate across multiple time periods
			if( msElapsed > 100 )
			{
				DWORD chunks = msElapsed / 100;
				for( DWORD i = 1; i < chunks; i++ )
				{
					CProgressData d;
					d.ms = lastTime + (i * 100);
					d.cpu = data.cpu;				// CPU time was already spread over the period
					d.bpsIn = data.bpsIn / chunks;	// split bandwidth evenly across the time slices
					d.mem = data.mem;				// just assign them all the same memory use (could interpolate but probably not worth it)
					progressData.AddTail(d);
				}

				data.bpsIn /= chunks;	// bandwidth is the only measure in the main chunk that needs to be adjusted
			}

			bool grabImage = false;
			if( captureVideo && windowUpdated && hBrowserWnd && IsWindow(hBrowserWnd) )
			{
				// see what time increment we are in
				// we go from 0.1 second to 1 second to 5 second intervals
				// as we get more and more screen shots
				DWORD minTime = 100;
				if( imageCount >= imageIncrements )
					minTime = 1000;
				if( imageCount >= imageIncrements * 2 )
					minTime = 5000;

				if( !lastImageTime || ((data.ms > lastImageTime) && (data.ms - lastImageTime) >= minTime) )
					grabImage = true;
			}

			if( grabImage )
			{
				windowUpdated = false;

				ATLTRACE(_T("[Pagetest] - Grabbing video frame : %d ms\n"), data.ms);

				// grab a screen shot of the window
				HDC src = GetDC(hBrowserWnd);
				if( src )
				{
					HDC dc = CreateCompatibleDC(src);
					if( dc )
					{
						CRect rect;
						GetWindowRect(hBrowserWnd, &rect);
						data.hBitmap = CreateCompatibleBitmap(src, rect.Width(), rect.Height()); 
						if( data.hBitmap )
						{
							HBITMAP hOriginal = (HBITMAP)SelectObject(dc, data.hBitmap);
							if( BitBlt(dc, 0, 0, rect.Width(), rect.Height(), src, 0, 0, SRCCOPY | CAPTUREBLT) )
							{
								imageCount++;
								lastImageTime = data.ms;
								if( !lastImageTime )
									lastImageTime = 1;
							}

							SelectObject(dc, hOriginal);
						}
						DeleteDC(dc);
					}
					ReleaseDC(hBrowserWnd, src);
				}
			}

			progressData.AddTail(data);
			lastTime = data.ms;
		}
	}

	LeaveCriticalSection(&csBackground);
}

/*-----------------------------------------------------------------------------
	Put a lock around when the window is being painnted so we don't get partial grabs
-----------------------------------------------------------------------------*/
void CTestState::OnBeginPaint(HWND hWnd)
{
//	if( hBrowserWnd && hWnd == hBrowserWnd )
//		EnterCriticalSection(&csBackground);
}

/*-----------------------------------------------------------------------------
-----------------------------------------------------------------------------*/
void CTestState::OnEndPaint(HWND hWnd)
{
//	if( hBrowserWnd && hWnd == hBrowserWnd )
//		LeaveCriticalSection(&csBackground);
}

