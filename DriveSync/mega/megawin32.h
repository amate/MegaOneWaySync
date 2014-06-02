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

#ifdef _WIN32
#ifndef MEGAWIN32_H
#define MEGAWIN32_H

#include <winhttp.h>
#include <mutex>
#include <thread>
#include <atomic>


struct WinHttpContext
{
	HINTERNET hConnect;       // connection handle
	HINTERNET hRequest;       // resource request handle
	DWORD postpos;

	std::thread	processThread;
	std::atomic_bool bCancel;
	std::string	data;

	WinHttpContext() : hConnect(NULL), hRequest(NULL), postpos(0) {
		bCancel = false;
	}
};

class WinHttpIO : public HttpIO
{
public:
	WinHttpIO();
	~WinHttpIO();

	// HttpIO
	void updatedstime();

	void post(HttpReq*, const char* data = nullptr, unsigned len = 0);
	void cancel(HttpReq*);

	m_off_t postpos(void*);

	int doio(void);
	void waitio(uint32_t);

protected:
	HINTERNET hSession;
	int completion;

private:
	static void _ProcessIO(HttpReq* req, WinHttpContext* cpContext);

};


class WinFileAccess : public FileAccess
{
	HANDLE hFile;

public:
	int fopen(const char*, int, int);
	int fread(string*, unsigned, unsigned, m_off_t);
	int fwrite(const byte*, unsigned, m_off_t);

	WinFileAccess();
	~WinFileAccess();
};

#endif
#endif
