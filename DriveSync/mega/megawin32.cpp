/*

MEGA SDK 2013-10-03 - sample application for the Win32 environment

Using WinHTTP for HTTP I/O and native Win32 file access with UTF-8 <-> WCHAR conversion

(c) 2013 by Mega Limited, Wellsford, New Zealand

Applications using the MEGA API must present a valid application key
and comply with the the rules set forth in the Terms of Service.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.

*/

#include "..\stdafx.h"
#include <stdarg.h>
#include <fcntl.h>
#include <stdio.h>
#include <thread>

#include <windows.h>
#include <winhttp.h>
#include <wchar.h>

#include <time.h>
#include <conio.h>

#define USE_VARARGS
#define PREFER_STDARG
//#include <readline/readline.h>

#include "mega.h"

#include "megacrypto.h"
#include "megaclient.h"
#include "megacli.h"
//#include "megaposix.h"
#include "megawin32.h"
#include "megabdb.h"

#include "..\Utility.h"

/////////////////////////////////////////////////////////////////////////////////////
// WinHttpIO

// HttpIO implementation using WinHTTP
WinHttpIO::WinHttpIO()
{
    // create the session handle using the default settings.
	hSession = WinHttpOpen(L"MEGA Client Access Engine/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	ATLASSERT(hSession);

	DWORD receiveTimeOut = 3 * 60 * 1000;	// 3min
	ATLVERIFY(WinHttpSetOption(hSession, WINHTTP_OPTION_RECEIVE_TIMEOUT, &receiveTimeOut, sizeof(receiveTimeOut)));

	completion = 0;
}

WinHttpIO::~WinHttpIO()
{
	WinHttpCloseHandle(hSession);
}

// update monotonously increasing timestamp in deciseconds
void WinHttpIO::updatedstime()
{
	ds = (dstime)(GetTickCount()/100);	// FIXME: Use GetTickCount64() instead
}

// POST request to URL
void WinHttpIO::post(HttpReq* req, const char* data /*= nullptr*/, unsigned len /*= 0*/)
{
	if (debug) {
		cout << "POST target URL: " << req->posturl << endl;

		if (req->binary) {
			cout << "[sending " << req->out->size() << " bytes of raw data]" << endl;
		} else {
			cout << "Sending: " << *req->out << endl;
		}
	}

	WinHttpContext* cpContext = new WinHttpContext;
	req->httpiohandle = (void*)cpContext;

	WCHAR szHost[256] = L"";
	URL_COMPONENTS urlComp = { sizeof(URL_COMPONENTS) };

	urlComp.lpszHostName = szHost;
	urlComp.dwHostNameLength = _countof(szHost);
    urlComp.dwUrlPathLength = (DWORD)-1;
    urlComp.dwSchemeLength = (DWORD)-1;

	try {
		// Crack the URL.
		std::wstring strURL = ConvertUTF16fromUTF8(req->posturl);
		BOOL bRet = WinHttpCrackUrl(strURL.c_str(), strURL.length(), 0, &urlComp);
		if (bRet == FALSE)
			throw std::exception("WinHttpCrackUrl failed");

		cpContext->hConnect = WinHttpConnect(hSession, szHost, urlComp.nPort, 0);
		if (cpContext->hConnect == NULL)
			throw std::exception("WinHttpConnect failed");

		cpContext->hRequest = WinHttpOpenRequest(cpContext->hConnect, L"POST", urlComp.lpszUrlPath, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
			(urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0);
		if (cpContext->hRequest == NULL)
			throw std::exception("WinHttpOpenRequest failed");

		if (data) {
			cpContext->data.assign(data, len);
		}
		req->status = REQ_INFLIGHT;
		cpContext->processThread = std::thread(_ProcessIO, req, cpContext);

		return;
	}
	catch (std::exception& e) {
		cout << e.what() << endl;
		ATLASSERT(FALSE);
	}

	req->status = REQ_FAILURE;
}

// cancel pending HTTP request
void WinHttpIO::cancel(HttpReq* req)
{
	if (req->httpiohandle) {
		WinHttpContext* cpContext = (WinHttpContext*)req->httpiohandle;
		cpContext->bCancel = true;
		if (cpContext->processThread.joinable())
			cpContext->processThread.join();

		if (cpContext->hRequest) {
			WinHttpCloseHandle(cpContext->hRequest);
		}
		if (cpContext->hConnect) 
			WinHttpCloseHandle(cpContext->hConnect);

		req->httpstatus = 0;
		req->status = REQ_FAILURE;

		delete (WinHttpContext*)req->httpiohandle;
		req->httpiohandle = NULL;
	}
}

// real-time progress information on POST data
m_off_t WinHttpIO::postpos(void* handle)
{
	WinHttpContext* cpContext = (WinHttpContext*)handle;
	return cpContext->postpos;
}

// process events
int WinHttpIO::doio()
{
	int done;

	done = completion;
	completion = 0;

	return done;
}

// wait for events (sockets, timeout + application events)
// ds specifies the maximum amount of time to wait in deciseconds (or ~0 if no timeout scheduled)
void WinHttpIO::waitio(dstime maxds)
{
	redisplay = 1;
}


void WinHttpIO::_ProcessIO(HttpReq* req, WinHttpContext* cpContext)
{
	enum ProcessStep {
		kStepStart,
		kStepSendRequest,
		kStepWriteData,
		kStepReceiveResponse,
		kStepQueryHeaders,
		kStepDataRead,
		kStepFinish,

	};
	ProcessStep step = kStepStart;
	LPVOID data;
	DWORD size;
	if (cpContext->data.length()) {
		data = (LPVOID)cpContext->data.data();
		size = (DWORD)cpContext->data.size();
	} else {
		data = (LPVOID)req->out->data();
		size = (DWORD)req->out->size();
	}
	try {
		while (cpContext->bCancel == false) {
			BOOL bRet = FALSE;
			switch (step) {
				case kStepStart:
				step = kStepSendRequest;
				break;

				case kStepSendRequest:
				{
					LPCWSTR pwszHeaders = REQ_JSON ? L"Content-Type: application/json"
						: L"Content-Type: application/octet-stream";
					bRet = WinHttpSendRequest(cpContext->hRequest, pwszHeaders, wcslen(pwszHeaders), WINHTTP_NO_REQUEST_DATA, 0, size, NULL);
					if (bRet == FALSE)
						throw std::exception("WinHttpSendRequest failed");
					if (size == 0) {
						step = kStepReceiveResponse;
					} else {
						step = kStepWriteData;
					}
				}
				break;

				case kStepWriteData:
				{
					enum { kChunkSize = 32000 };	// 32kb
					LPCVOID buffPos = static_cast<BYTE*>(data) + cpContext->postpos;
					DWORD dwBytesWritten = 0;
					DWORD dwBuffSize = kChunkSize;
					DWORD restSize = size - cpContext->postpos;
					if (restSize < kChunkSize)
						dwBuffSize = restSize;
					bRet = WinHttpWriteData(cpContext->hRequest, buffPos, dwBuffSize, &dwBytesWritten);
					if (bRet == FALSE)
						throw std::exception("WinHttpWriteData failed");

					cpContext->postpos += dwBytesWritten;
					if (cpContext->postpos == size)
						step = kStepReceiveResponse;
				}
				break;

				case kStepReceiveResponse:
				{
					bRet = ::WinHttpReceiveResponse(cpContext->hRequest, NULL);
					if (bRet == FALSE) {
						DWORD error = GetLastError();
						throw std::exception("WinHttpReceiveResponse failed");
					}
					step = kStepQueryHeaders;
				}
				break;

				case kStepQueryHeaders:
				{
					// ステータスコードを調べる
					DWORD statusCode = 0;
					DWORD statusCodeSize = sizeof(statusCode);
					bRet = ::WinHttpQueryHeaders(cpContext->hRequest,
						WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
						WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusCodeSize, WINHTTP_NO_HEADER_INDEX);
					if (bRet == FALSE)
						throw std::exception("WinHttpQueryHeaders failed");

					req->httpstatus = (int)statusCode;
					step = kStepDataRead;
				}
				break;

				case kStepDataRead:
				{
					DWORD	dwAvailableSize = 0;
					if (::WinHttpQueryDataAvailable(cpContext->hRequest, &dwAvailableSize) == FALSE)
						throw std::exception("WinHttpQueryDataAvailable failed");

					if (dwAvailableSize == 0) {	// もうデータがない
						step = kStepFinish;
						break;
					}

					unsigned size = (unsigned)dwAvailableSize;
					byte* buff = req->reserveput(&size);
					DWORD dwDownloaded = 0;
					bRet = ::WinHttpReadData(cpContext->hRequest, buff, size, &dwDownloaded);
					if (bRet == FALSE)
						throw std::exception("WinHttpReadData failed");
					ATLASSERT(size == dwDownloaded);
					req->completeput((unsigned)dwDownloaded);
				}
				break;

				case kStepFinish:
				{
					req->status = req->httpstatus == 200 ? REQ_SUCCESS : REQ_FAILURE;
					return;
				}
			}
		}

	}
	catch (std::exception& e) {
		cout << e.what() << endl;
	}
	req->status = REQ_FAILURE;
}



////////////////////////////////////////////////////////////////////////////////////////
// WinFileAccess

WinFileAccess::WinFileAccess()
{
	hFile = INVALID_HANDLE_VALUE;
}

WinFileAccess::~WinFileAccess()
{
	if (hFile != INVALID_HANDLE_VALUE) CloseHandle(hFile);
}

int WinFileAccess::fread(string* dst, unsigned len, unsigned pad, m_off_t pos)
{
	DWORD dwRead;

	if (!SetFilePointerEx(hFile,*(LARGE_INTEGER*)&pos,NULL,FILE_BEGIN)) return 0;

	dst->resize(len+pad);

	memset((char*)dst->data(),0,len+pad);

	if (ReadFile(hFile,(LPVOID)dst->data(),(DWORD)len,&dwRead,NULL) && dwRead == len)
	{
		memset((char*)dst->data()+len,0,pad);
		return 1;
	}

	return 0;
}

int WinFileAccess::fwrite(const byte* data, unsigned len, m_off_t pos)
{
	DWORD dwWritten;

	if (!SetFilePointerEx(hFile,*(LARGE_INTEGER*)&pos,NULL,FILE_BEGIN)) return 0;

	return WriteFile(hFile,(LPCVOID)data,(DWORD)len,&dwWritten,NULL) && dwWritten == len;
}

// 定義はUtility.cppに
time_t FileTime_to_POSIX(const FILETIME* ft);

int WinFileAccess::fopen(const char* f, int read, int write)
{
	WCHAR wszFileName[MAX_PATH+1];

	if (!MultiByteToWideChar(CP_UTF8, 0, f, -1, wszFileName, sizeof wszFileName / sizeof *wszFileName)) return 0;

	hFile = CreateFileW(wszFileName, read ? GENERIC_READ : GENERIC_WRITE, FILE_SHARE_WRITE | FILE_SHARE_READ, NULL, read ? OPEN_EXISTING : OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

	if (hFile == INVALID_HANDLE_VALUE) return 0;

	if (read)
	{
		BY_HANDLE_FILE_INFORMATION fi;

		if (!GetFileInformationByHandle(hFile,&fi)) return 0;

		size = ((m_off_t)fi.nFileSizeHigh << 32)+(m_off_t)fi.nFileSizeLow;

		mtime = FileTime_to_POSIX(&fi.ftLastWriteTime);
	}
	else if (!GetFileSizeEx(hFile,(LARGE_INTEGER*)&size)) return 0;

	return 1;
}

int rename_file(const char* oldname, const char* newname)
{
	WCHAR wszOldFileName[MAX_PATH+1], wszNewFileName[MAX_PATH+1];

	if (!MultiByteToWideChar(CP_UTF8,0,oldname,-1,wszOldFileName,sizeof wszOldFileName/sizeof *wszOldFileName)) return 0;
	if (!MultiByteToWideChar(CP_UTF8,0,newname,-1,wszNewFileName,sizeof wszNewFileName/sizeof *wszNewFileName)) return 0;

	return MoveFileExW(wszOldFileName,wszNewFileName,MOVEFILE_REPLACE_EXISTING);
}

int unlink_file(const char* name)
{
	WCHAR wszFileName[MAX_PATH+1];

    if (!MultiByteToWideChar(CP_UTF8,0,name,-1,wszFileName,sizeof wszFileName/sizeof *wszFileName)) return 0;

	return DeleteFileW(wszFileName);
}

int change_dir(const char* name)
{
	WCHAR wszPath[MAX_PATH+1];

    if (!MultiByteToWideChar(CP_UTF8,0,name,-1,wszPath,sizeof wszPath/sizeof *wszPath)) return 0;

	return _wchdir(wszPath);
}

FileAccess* DemoApp::newfile()
{
	return new WinFileAccess();
}

int globenqueue(const char* path, const char* newname, handle target,  const char* targetuser)
{
	WIN32_FIND_DATAW FindFileData;
	int pathlen = strlen(path)+1;
	WCHAR* szPath = new WCHAR[pathlen];
	char utf8buf[MAX_PATH*4+1];
	HANDLE hFindFile;
	int total = 0;

    if (MultiByteToWideChar(CP_UTF8,0,path,-1,szPath,pathlen))
	{
		hFindFile = FindFirstFileW(szPath,&FindFileData);

		if (hFindFile != INVALID_HANDLE_VALUE)
		{
			do {
				if (!(FindFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
				{
					if (WideCharToMultiByte(CP_UTF8,0,FindFileData.cFileName,-1,utf8buf,sizeof utf8buf,NULL,NULL))
					{
						client->putq.push_back(new AppFilePut(utf8buf,target,targetuser,newname));
						total++;
					}
				}
			} while (FindNextFileW(hFindFile,&FindFileData));
		}
	}

	FindClose(hFindFile);

	return total;
}

// FIXME: implement terminal echo control
void term_init()
{
	// get terminal config at startup
}

void term_restore()
{
	// restore startup config
}

void term_echo(int echo)
{
	if (echo)
	{
		// FIXME: enable echo
	}
	else
	{
		// FIXME: disable echo
	}
}

int main()
{
	// instantiate app components: the callback processor (DemoApp),
	// the HTTP I/O engine (WinHttpIO) and the MegaClient itself
	const char* kAppkey = "iR0XWCLa";
	client = new MegaClient(new DemoApp, new WinHttpIO, new BdbAccess, kAppkey);

	megacli();
}
