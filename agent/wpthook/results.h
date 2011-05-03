/******************************************************************************
Copyright (c) 2010, Google Inc.
All rights reserved.

Redistribution and use in source and binary forms, with or without 
modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice, 
      this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright notice,
      this list of conditions and the following disclaimer in the documentation
      and/or other materials provided with the distribution.
    * Neither the name of the <ORGANIZATION> nor the names of its contributors 
    may be used to endorse or promote products derived from this software 
    without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" 
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE 
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE 
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL 
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR 
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER 
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, 
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE 
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
******************************************************************************/

#pragma once

class Requests;
class Request;
class TestState;
class TrackSockets;
class ScreenCapture;
class CxImage;
class WptTest;

class Results {
public:
  Results(TestState& test_state, WptTest& test, Requests& requests, 
          TrackSockets& sockets, ScreenCapture& screen_capture);
  ~Results(void);

  void Reset(void);
  void Save(void);

  // test information
  CString _url;

private:
  CString     _file_base;
  Requests&   _requests;
  TestState&  _test_state;
  TrackSockets& _sockets;
  ScreenCapture& _screen_capture;
  WptTest&      _test;
  bool        _saved;

  void SavePageData(void);
  void SaveRequests(void);
  void SaveRequest(HANDLE file, HANDLE headers, Request * request, int index);
  void SaveImages(void);
  void SaveVideo(void);
  void SaveProgressData(void);
  void SaveImage(CxImage& image, CString file, bool shrink, BYTE quality);
  bool ImagesAreDifferent(CxImage * img1, CxImage* img2);
};

