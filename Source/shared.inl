/*
  Unity Capture
  Copyright (c) 2018 Bernhard Schelling

  Based on UnityCam
  https://github.com/mrayy/UnityCam
  Copyright (c) 2016 MHD Yamen Saraiji
*/

#define _HAS_EXCEPTIONS 0
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <initguid.h>
#include <stdint.h>

#define MAX_SHARED_IMAGE_SIZE (3840 * 2160 * 3) //4K

#if _DEBUG
#define UCASSERT(cond) ((cond) ? ((void)0) : *(volatile int*)0 = 0xbad|(OutputDebugStringA("[FAILED ASSERT] " #cond "\n"),1))
#else
#define UCASSERT(cond) ((void)0)
#endif

#define CS_NAME_EVENT_WANT  "UnityCapture_Want"
#define CS_NAME_EVENT_SENT  "UnityCapture_Sent"
#define CS_NAME_MUTEX       "UnityCapture_Mutx"
#define CS_NAME_SHARED_DATA "UnityCapture_Data"

struct SharedImageMemory
{
	SharedImageMemory()
	{
		memset(this, 0, sizeof(*this));
	}

	~SharedImageMemory()
	{
		if (m_pMutex) delete m_pMutex;
		if (m_pUnscaledBuf) delete[] m_pUnscaledBuf;
		if (m_hWantFrameEvent) CloseHandle(m_hWantFrameEvent);
		if (m_hSentFrameEvent) CloseHandle(m_hSentFrameEvent);
		if (m_hSharedFile) CloseHandle(m_hSharedFile);
	}

	enum EResizeMode { RESIZEMODE_DISABLED = 0, RESIZEMODE_LINEAR = 1 };

	enum EReceiveResult { RECEIVERES_CAPTUREINACTIVE, RECEIVERES_NEWFRAME, RECEIVERES_OLDFRAME };
	EReceiveResult Receive(unsigned char* pOutBuf, int OutWidth, int OutHeight, bool *pNeedResize, EResizeMode *pResizeMode, unsigned char** ppUnscaledBuf, int* pUnscaledWidth, int* pUnscaledHeight)
	{
		if (!Open(true)) return RECEIVERES_CAPTUREINACTIVE;

		SetEvent(m_hWantFrameEvent);
		bool IsNewFrame = (WaitForSingleObject(m_hSentFrameEvent, 200) == WAIT_OBJECT_0);

		m_pMutex->Lock();
		const size_t imageSize = (size_t)m_pSharedBuf->width * (size_t)m_pSharedBuf->height * 3;
		*pUnscaledWidth = m_pSharedBuf->width;
		*pUnscaledHeight = m_pSharedBuf->height;
		*pNeedResize =  (m_pSharedBuf->width != OutWidth || m_pSharedBuf->height != OutHeight);
		*pResizeMode = (EResizeMode)m_pSharedBuf->resizemode;
		if (!*pNeedResize) memcpy(pOutBuf, m_pSharedBuf->data, imageSize);
		else if (*pResizeMode != RESIZEMODE_DISABLED)
		{
			if (m_iUnscaledBufSize != imageSize)
			{
				if (m_pUnscaledBuf) delete[] m_pUnscaledBuf;
				m_pUnscaledBuf = new unsigned char[imageSize];
				m_iUnscaledBufSize = imageSize;
			}
			memcpy((*ppUnscaledBuf = m_pUnscaledBuf), m_pSharedBuf->data, imageSize);
		}
		m_pMutex->Unlock();

		return (IsNewFrame ? RECEIVERES_NEWFRAME : RECEIVERES_OLDFRAME);
	}

	//void ReceiveUnscaled(unsigned char** ppUnscaledBuf, int* pUnscaledWidth, int* pUnscaledHeight)
	//{
	//	UCASSERT(ppUnscaledBuf && pUnscaledWidth && pUnscaledHeight && Open(true));
	//	m_pMutex->Lock();
	//	if (m_iUnscaledBufSize != m_pSharedBuf->size)
	//	{
	//		if (m_pUnscaledBuf) delete[] m_pUnscaledBuf;
	//		m_pUnscaledBuf = new unsigned char[m_pSharedBuf->size];
	//		m_iUnscaledBufSize = m_pSharedBuf->size;
	//	}
	//	*pUnscaledWidth = m_pSharedBuf->width;
	//	*pUnscaledHeight = m_pSharedBuf->height;
	//	memcpy((*ppUnscaledBuf = m_pUnscaledBuf), m_pSharedBuf->data, m_pSharedBuf->size);
	//	m_pMutex->Unlock();
	//}

	enum ESendResult { SENDRES_CAPTUREINACTIVE, SENDRES_TOOLARGE, SENDRES_WARN_FRAMESKIP, SENDRES_OK };
	ESendResult Send(int width, int height, EResizeMode resizemode, const unsigned char* buffer)
	{
		UCASSERT(buffer);
		if (!Open(false)) return SENDRES_CAPTUREINACTIVE;

		DWORD imageSize = (DWORD)width * (DWORD)height * 3;
		if (m_pSharedBuf->maxSize < imageSize) return SENDRES_TOOLARGE;

		m_pMutex->Lock();
		m_pSharedBuf->width = width;
		m_pSharedBuf->height = height;
		m_pSharedBuf->resizemode = resizemode;
		memcpy(m_pSharedBuf->data, buffer, imageSize);
		m_pMutex->Unlock();

		SetEvent(m_hSentFrameEvent);
		bool DidSkipFrame = (WaitForSingleObject(m_hWantFrameEvent, 0) != WAIT_OBJECT_0);

		return (DidSkipFrame ? SENDRES_WARN_FRAMESKIP : SENDRES_OK);
	}

private:
	bool Open(bool ForReceiving)
	{
		if (m_pSharedBuf) return true; //already open

		if (!m_pMutex)
		{
			HRESULT hr = S_OK;
			m_pMutex = new SharedMutex(CS_NAME_MUTEX, ForReceiving, &hr);
			if (FAILED(hr)) { delete m_pMutex; m_pMutex = NULL; }
			if (!m_pMutex) return false;
		}

		m_pMutex->Lock();
		struct UnlockAtReturn { ~UnlockAtReturn() { m->Unlock(); }; SharedMutex* m; } cs = { m_pMutex };

		if (!m_hWantFrameEvent)
		{
			if (ForReceiving) m_hWantFrameEvent = OpenEventA(EVENT_MODIFY_STATE, FALSE, CS_NAME_EVENT_WANT);
			else              m_hWantFrameEvent = CreateEventA(NULL, FALSE, FALSE,      CS_NAME_EVENT_WANT);
			if (!m_hWantFrameEvent) return false;
		}

		if (!m_hSentFrameEvent)
		{
			if (ForReceiving) m_hSentFrameEvent = CreateEventA(NULL, FALSE, FALSE,      CS_NAME_EVENT_SENT);
			else              m_hSentFrameEvent = OpenEventA(EVENT_MODIFY_STATE, FALSE, CS_NAME_EVENT_SENT);
			if (!m_hSentFrameEvent) return false;
		}

		if (!m_hSharedFile)
		{
			if (ForReceiving) m_hSharedFile = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, NULL, sizeof(SharedMemHeader) + MAX_SHARED_IMAGE_SIZE, CS_NAME_SHARED_DATA);
			else              m_hSharedFile = OpenFileMappingA(FILE_MAP_WRITE, FALSE, CS_NAME_SHARED_DATA);
			if (!m_hSharedFile) return false;
		}

		m_pSharedBuf = (SharedMemHeader*)MapViewOfFile(m_hSharedFile, FILE_MAP_WRITE, 0, 0, 0);
		if (!m_pSharedBuf) return false;

		if (ForReceiving && m_pSharedBuf->maxSize != MAX_SHARED_IMAGE_SIZE)
		{
			ZeroMemory(m_pSharedBuf, sizeof(SharedMemHeader) + MAX_SHARED_IMAGE_SIZE);
			m_pSharedBuf->maxSize = MAX_SHARED_IMAGE_SIZE;
		}
		return true;
	}

	struct SharedMutex
	{
		SharedMutex(const char* name, bool create, HRESULT* phr) { *phr = (!(h = (create ? CreateMutexA(NULL, FALSE, name) : OpenMutexA(SYNCHRONIZE, FALSE, name))) ? E_UNEXPECTED : S_OK); }
		~SharedMutex() { CloseHandle(h); }
		void Lock() { WaitForSingleObject(h, INFINITE); }
		void Unlock() { ReleaseMutex(h); }
		private: HANDLE h;
	};

	struct SharedMemHeader
	{
		DWORD maxSize;
		int width;
		int height;
		int resizemode;
		unsigned char data[1];
	};


	SharedMutex* m_pMutex;
	HANDLE m_hWantFrameEvent;
	HANDLE m_hSentFrameEvent;
	HANDLE m_hSharedFile;
	SharedMemHeader* m_pSharedBuf;

	size_t m_iUnscaledBufSize;
	unsigned char* m_pUnscaledBuf;
};
