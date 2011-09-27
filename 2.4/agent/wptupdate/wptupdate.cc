#define WINVER 0x0500
#define _WIN32_WINNT 0x0501
#define _WIN32_WINDOWS 0x0410
#define _WIN32_IE 0x0500
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <stdlib.h>
#include <malloc.h>
#include <memory.h>
#include <tchar.h>

#include <shellapi.h>
#include <shlwapi.h>
#include <WtsApi32.h>
#include <Psapi.h>

bool FindWpt(TCHAR * wpt_path);
void TerminateProcs(void);
void Reboot(void);

int APIENTRY _tWinMain(HINSTANCE hInstance,
                     HINSTANCE hPrevInstance,
                     LPTSTR    lpCmdLine,
                     int       nCmdShow) {
  UNREFERENCED_PARAMETER(hPrevInstance);
  UNREFERENCED_PARAMETER(lpCmdLine);

  // figure out the path we are updating to (where urlblast is running)
  TCHAR wpt_path[MAX_PATH];
  if (FindWpt(wpt_path)) {
    TerminateProcs();

    // figure out the source path
    TCHAR this_proc[MAX_PATH];
    TCHAR src_path[MAX_PATH];
    GetModuleFileName(NULL, this_proc, MAX_PATH);
    lstrcpy( src_path, this_proc );
    *PathFindFileName(src_path) = 0;

    // copy everything in this directory to the destination path
    TCHAR search[MAX_PATH];
    TCHAR dest_file[MAX_PATH];
    TCHAR src_file[MAX_PATH];

    lstrcpy(search, src_path);
    lstrcat(search, _T("*.*"));
    WIN32_FIND_DATA fd;
    HANDLE find_handle = FindFirstFile(search, &fd);
    if (find_handle != INVALID_HANDLE_VALUE) {
      do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
          lstrcmpi(fd.cFileName, _T("wptupdate.exe")) && 
          lstrcmpi(fd.cFileName, _T("wptupdate.ini"))) {
          lstrcpy( src_file, src_path );
          lstrcat( src_file, fd.cFileName );
          lstrcpy( dest_file, wpt_path );
          lstrcat( dest_file, fd.cFileName );
          CopyFile(src_file, dest_file, FALSE);
        }
      } while( FindNextFile(find_handle, &fd) );
      FindClose(find_handle);
    }

    // Start wptdriver back up
    lstrcat( wpt_path, _T("wptdriver.exe") );
    if( (int)ShellExecute(NULL, NULL, wpt_path, NULL, wpt_path, 
          SW_SHOWMINNOACTIVE) <= 32 )
      Reboot();
  }

  return 0;
}

/*-----------------------------------------------------------------------------
-----------------------------------------------------------------------------*/
bool FindWpt(TCHAR * wpt_path) {
  bool found = false;
  *wpt_path = 0;

  WTS_PROCESS_INFO * proc = NULL;
  DWORD count = 0;
  if (WTSEnumerateProcesses(WTS_CURRENT_SERVER_HANDLE, 0, 1, &proc, &count)) {
    for (DWORD i = 0; i < count && !found ; i++) {
      TCHAR * file = PathFindFileName(proc[i].pProcessName);
      if (!lstrcmpi(file, _T("wptdriver.exe"))) {
        TCHAR path[MAX_PATH];
        HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | 
                                    PROCESS_VM_READ,
                                    FALSE, proc[i].ProcessId);
        if (process) {
          if (GetModuleFileNameEx(process, NULL, path, MAX_PATH)) {
            *PathFindFileName(path) = 0;
            lstrcpy( wpt_path, path );
            if( lstrlen(wpt_path) )
              found = true;
          }

          CloseHandle(process);
        }
      }
    }

    WTSFreeMemory(proc);
  }

  return found;
}

/*-----------------------------------------------------------------------------
  Kill any processes that we need closed
-----------------------------------------------------------------------------*/
void TerminateProcs(void) {
  HWND desktop = ::GetDesktopWindow();
  HWND wnd = ::GetWindow(desktop, GW_CHILD);
  TCHAR szTitle[1025];
  const TCHAR * szClose[] = { 
    _T("wptdriver")
    , _T("google chrome")
  };

  // Send close messages to everything
  while (wnd) {
    if (::IsWindowVisible(wnd)) {
      if (::GetWindowText( wnd, szTitle, 1024)) {
        bool bKill = false;
        _tcslwr_s(szTitle, _countof(szTitle));
        for (int i = 0; i < _countof(szClose) && !bKill; i++) {
          if (_tcsstr(szTitle, szClose[i]))
            bKill = true;
        }
        
        if( bKill )
          ::PostMessage(wnd,WM_CLOSE,0,0);
      }
    }

    wnd = ::GetWindow(wnd, GW_HWNDNEXT);
  }

  // let our process kill processes from other users
  HANDLE token;
  if (OpenProcessToken(GetCurrentProcess() , 
                          TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY , &token)) {
    TOKEN_PRIVILEGES tp;
    if (LookupPrivilegeValue(NULL, SE_DEBUG_NAME, &tp.Privileges[0].Luid)) {
      tp.PrivilegeCount = 1;
      tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
      AdjustTokenPrivileges(token, FALSE, &tp, 0, (PTOKEN_PRIVILEGES)0, 0);
    }
    CloseHandle(token);
  }

  // go through and kill any procs that are still running
  // (wait long enough for them to exit gracefully first)
  const TCHAR * procs[] = { 
    _T("wptdriver.exe")
    , _T("chrome.exe")
  };

  WTS_PROCESS_INFO * proc = NULL;
  DWORD count = 0;
  if (WTSEnumerateProcesses(WTS_CURRENT_SERVER_HANDLE, 0, 1, &proc, &count)) {
    for (DWORD i = 0; i < count; i++) {
      TCHAR * file = proc[i].pProcessName;
      bool kill = false;
      for (int j = 0; j < _countof(procs) && !kill; j++) {
        if (!lstrcmpi(PathFindFileName(proc[i].pProcessName), procs[j]))
          kill = true;
      }

      if( kill ) {
        HANDLE process = OpenProcess(SYNCHRONIZE | PROCESS_TERMINATE, FALSE, 
                                      proc[i].ProcessId);
        if (process) {
          // give it 2 minutes to exit on it's own
          if (WaitForSingleObject(process, 120000) != WAIT_OBJECT_0)
            TerminateProcess(process, 0);
          CloseHandle(process);
        }
      }
    }

    WTSFreeMemory(proc);
  }
}

/*-----------------------------------------------------------------------------
  Reboot the system
-----------------------------------------------------------------------------*/
void Reboot(void) {
  HANDLE token;
  if (OpenProcessToken(GetCurrentProcess(), 
                  TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token))
  {
    TOKEN_PRIVILEGES tp;
    if (LookupPrivilegeValue(NULL, SE_SHUTDOWN_NAME, &tp.Privileges[0].Luid)) {
      tp.PrivilegeCount = 1;
      tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
      AdjustTokenPrivileges(token, FALSE, &tp, 0, (PTOKEN_PRIVILEGES)0 ,0 );
    }
    CloseHandle(token);
  }
  
  InitiateSystemShutdown(NULL, _T("wptdriver update installed."),0,TRUE,TRUE);
}
