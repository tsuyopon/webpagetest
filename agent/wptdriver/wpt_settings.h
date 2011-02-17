#pragma once

// constants
const DWORD EXIT_TIMEOUT = 120000;

// default settings
const DWORD DEFAULT_TEST_TIMEOUT = 120;
const DWORD DEFAULT_STARTUP_DELAY = 10;
const DWORD DEFAULT_POLLING_DELAY = 15;

// conversions
const DWORD SECONDS_TO_MS = 1000;

// dynamic settings loaded from file
class WptSettings
{
public:
  WptSettings(void);
  ~WptSettings(void);
  bool Load(void);

  CString _server;
  CString _location;
  CString _key;
  DWORD   _timeout;
  DWORD   _startup_delay;
  DWORD   _polling_delay;

  // browsers
  CString _browser_chrome;
  CString _browser_firefox;
  CString _browser_ie;
};
