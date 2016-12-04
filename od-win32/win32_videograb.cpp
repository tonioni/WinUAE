
/* UAE Win32 Video frame grabber support
 * Toni Wilen 2016
 */

#include "sysconfig.h"

#include <stdlib.h>
#include <stdarg.h>

#include "sysdeps.h"
#include "options.h"

#include <windows.h>
#include <dshow.h>
#include <atlcomcli.h>

#include "videograb.h"

#pragma comment(lib,"Strmiids.lib") 

// following have been removed from newer SDKs

static const IID IID_ISampleGrabber = { 0x6B652FFF, 0x11FE, 0x4fce, { 0x92, 0xAD, 0x02, 0x66, 0xB5, 0xD7, 0xC7, 0x8F } };
static const CLSID CLSID_SampleGrabber = { 0xC1F400A0, 0x3F08, 0x11d3, { 0x9F, 0x0B, 0x00, 0x60, 0x08, 0x03, 0x9E, 0x37 } };
static const CLSID CLSID_NullRenderer = { 0xC1F400A4, 0x3F08, 0x11d3, { 0x9F, 0x0B, 0x00, 0x60, 0x08, 0x03, 0x9E, 0x37 } };

interface ISampleGrabberCB : public IUnknown
{
	virtual STDMETHODIMP SampleCB(double SampleTime, IMediaSample *pSample) = 0;
	virtual STDMETHODIMP BufferCB(double SampleTime, BYTE *pBuffer, long BufferLen) = 0;
};
interface ISampleGrabber : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE SetOneShot(BOOL OneShot) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetMediaType(const AM_MEDIA_TYPE *pType) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetConnectedMediaType(AM_MEDIA_TYPE *pType) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetBufferSamples(BOOL BufferThem) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCurrentBuffer(long *pBufferSize, long *pBuffer) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCurrentSample(IMediaSample **ppSample) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetCallback(ISampleGrabberCB *pCallback, long WhichMethodToCallback) = 0;
};

// based on code from: http://forum.devmaster.net/t/generating-textures-from-video-frames/8259

static CComPtr<ICaptureGraphBuilder2> graphBuilder;
static CComPtr<IFilterGraph2> filterGraph;
static CComPtr<ISampleGrabber> sampleGrabber;
static CComPtr<IMediaControl> mediaControl;
static CComPtr<IMediaSeeking> mediaSeeking;
static CComPtr<IMediaEvent> mediaEvent;
static CComPtr<IBasicAudio> audio;
static bool videoInitialized;
static bool videoPaused;
static long *frameBuffer;
static long bufferSize;
static int videoWidth, videoHeight;

void uninitvideograb(void)
{
	write_log(_T("uninitvideograb\n"));

	videoInitialized = false;
	videoPaused = false;

	sampleGrabber.Release();
	mediaSeeking.Release();
	mediaEvent.Release();
	audio.Release();
	if (mediaControl) {
		mediaControl->Stop();
	}
	mediaControl.Release();
	filterGraph.Release();
	graphBuilder.Release();

	delete[] frameBuffer;
	frameBuffer = NULL;
}

static void FindPin(IBaseFilter* baseFilter, PIN_DIRECTION direction, int pinNumber, IPin** destPin)
{
	CComPtr<IEnumPins> enumPins;

	*destPin = NULL;

	if (SUCCEEDED(baseFilter->EnumPins(&enumPins))) {
		ULONG numFound;
		IPin* tmpPin;

		while (SUCCEEDED(enumPins->Next(1, &tmpPin, &numFound))) {
			PIN_DIRECTION pinDirection;

			tmpPin->QueryDirection(&pinDirection);
			if (pinDirection == direction) {
				if (pinNumber == 0) {
					// Return the pin's interface
					*destPin = tmpPin;
					break;
				}
				pinNumber--;
			}
			tmpPin->Release();
		}
	}
}

static bool ConnectPins(IBaseFilter* outputFilter, unsigned int outputNum, IBaseFilter* inputFilter, unsigned int inputNum)
{
	CComPtr<IPin> inputPin;
	CComPtr<IPin> outputPin;

	if (!outputFilter || !inputFilter) {
		write_log(_T("ConnectPins OUT=%d IN=%d\n"), outputFilter != 0, inputFilter != 0);
		return false;
	}

	FindPin(outputFilter, PINDIR_OUTPUT, outputNum, &outputPin);
	FindPin(inputFilter, PINDIR_INPUT, inputNum, &inputPin);

	if (inputPin && outputPin) {
		HRESULT hr = filterGraph->Connect(outputPin, inputPin);
		if (SUCCEEDED(hr))
			return true;
		write_log(_T("ConnectPins Connect %08x\n"), hr);
	} else {
		write_log(_T("ConnectPins OUTPIN=%d INPIN=%d\n"), outputPin != 0, inputPin != 0);

	}
	return false;
}

bool initvideograb(const TCHAR *filename)
{
	HRESULT hr;

	uninitvideograb();

	write_log(_T("initvideograb '%s'\n"), filename ? filename : _T("<null>"));

	graphBuilder.CoCreateInstance(CLSID_CaptureGraphBuilder2);
	filterGraph.CoCreateInstance(CLSID_FilterGraph);
	graphBuilder->SetFiltergraph(filterGraph);
	CComPtr<IBaseFilter> sourceFilter;

	if (filename == NULL || !filename[0]) {
		filename = NULL;
		// capture device mode
		IMoniker *pMoniker;
		CComPtr<ICreateDevEnum> pCreateDevEnum;
		hr = CoCreateInstance(CLSID_SystemDeviceEnum, NULL, CLSCTX_INPROC, IID_ICreateDevEnum, (void**)&pCreateDevEnum);
		if (FAILED(hr)) {
			write_log(_T("CLSID_SystemDeviceEnum IID_ICreateDevEnum failed %08x\n"), hr);
			uninitvideograb();
			return false;
		}
		CComPtr<IEnumMoniker> pEmum;
		hr = pCreateDevEnum->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, &pEmum, 0);
		if (FAILED(hr)) {
			write_log(_T("CreateClassEnumerator CLSID_VideoInputDeviceCategory failed %08x\n"), hr);
			uninitvideograb();
			return false;
		}
		if (hr == S_FALSE) {
			write_log(_T("initvideograb CreateDevEnum: didn't find any capture devices.\n"));
			uninitvideograb();
			return false;
		}
		pEmum->Reset();
		ULONG cFetched = 0;
		//Take the first capture device found
		hr = pEmum->Next(1, &pMoniker, &cFetched);
		if (FAILED(hr)) {
			write_log(_T("initvideograb Next: didn't find any capture devices.\n"));
			uninitvideograb();
			return false;
		}
		pMoniker->BindToObject(NULL, NULL, IID_IBaseFilter, (void**)&sourceFilter);
		pMoniker->Release();
		filterGraph->AddFilter(sourceFilter, L"Video Capture");
	}

	// Create the Sample Grabber which we will use
	// To take each frame for texture generation
	CComPtr<IBaseFilter> grabberFilter;
	grabberFilter.CoCreateInstance(CLSID_SampleGrabber);
	grabberFilter->QueryInterface(IID_ISampleGrabber, reinterpret_cast<void**>(&sampleGrabber));

	hr = filterGraph->AddFilter(grabberFilter, L"Sample Grabber");

	// We have to set the 24-bit RGB desire here
	// So that the proper conversion filters
	// Are added automatically.
	AM_MEDIA_TYPE desiredType;
	memset(&desiredType, 0, sizeof(desiredType));
	desiredType.majortype = MEDIATYPE_Video;
	desiredType.subtype = MEDIASUBTYPE_RGB24;
	desiredType.formattype = FORMAT_VideoInfo;

	hr = sampleGrabber->SetMediaType(&desiredType);
	hr = sampleGrabber->SetBufferSamples(TRUE);

	if (filename) {
		hr = filterGraph->RenderFile(filename, NULL);
		if (FAILED(hr)) {
			uninitvideograb();
			return false;
		}
	}

	if (!filename) {
		// Use pin connection methods instead of 
		// ICaptureGraphBuilder::RenderStream because of
		// the SampleGrabber setting we're using.
		if (!ConnectPins(sourceFilter, 0, grabberFilter, 0)) {
			uninitvideograb();
			return false;
		}
	}

	// A Null Renderer does not display the video
	// But it allows the Sample Grabber to run
	// And it will keep proper playback timing
	// Unless specified otherwise.
	CComPtr<IBaseFilter> nullRenderer;
	nullRenderer.CoCreateInstance(CLSID_NullRenderer);

	hr = filterGraph->AddFilter(nullRenderer, L"Null Renderer");
	if (FAILED(hr)) {
		uninitvideograb();
		return false;
	}

	if (!filename) {
		if (!ConnectPins(grabberFilter, 0, nullRenderer, 0)) {
			uninitvideograb();
			return false;
		}
	}

	// Just a little trick so that we don't have to know
	// The video resolution when calling this method.
	bool mediaConnected = false;
	AM_MEDIA_TYPE connectedType;
	hr = sampleGrabber->GetConnectedMediaType(&connectedType);
	if (SUCCEEDED(hr)) {
		if (connectedType.formattype == FORMAT_VideoInfo) {
			VIDEOINFOHEADER* infoHeader = (VIDEOINFOHEADER*)connectedType.pbFormat;
			videoWidth = infoHeader->bmiHeader.biWidth;
			videoHeight = infoHeader->bmiHeader.biHeight;
			mediaConnected = true;
		}
		CoTaskMemFree(connectedType.pbFormat);
	}

	if (!mediaConnected) {
		uninitvideograb();
		return false;
	}

	if (filename) {
		// based on ofDirectShowPlayer
		IPin* pinIn = NULL;
		IPin* pinOut = NULL;
		IBaseFilter * m_pVideoRenderer;
		hr = filterGraph->FindFilterByName(_T("Video Renderer"), &m_pVideoRenderer);
		if (FAILED(hr)) {
			write_log(_T("FindFilterByName failed: %08x\n"), hr);
			uninitvideograb();
			return false;
		}
		hr = grabberFilter->FindPin(_T("Out"), &pinOut);
		if (FAILED(hr)) {
			write_log(_T("FindPin Out failed: %08x\n"), hr);
			m_pVideoRenderer->Release();
			uninitvideograb();
			return false;
		}
		hr = pinOut->Disconnect();
		if (FAILED(hr)) {
			write_log(_T("Disconnect failed: %08x\n"), hr);
			m_pVideoRenderer->Release();
			uninitvideograb();
			return false;
		}
		hr = filterGraph->RemoveFilter(m_pVideoRenderer);
		if (FAILED(hr)) {
			write_log(_T("RemoveFilter failed: %08x\n"), hr);
			m_pVideoRenderer->Release();
			uninitvideograb();
			return false;
		}
		m_pVideoRenderer->Release();
		hr = nullRenderer->FindPin(_T("In"), &pinIn);
		if (FAILED(hr)) {
			write_log(_T("FindPin In failed: %08x\n"), hr);
			uninitvideograb();
			return false;
		}
		hr = pinOut->Connect(pinIn, NULL);
		if (FAILED(hr)) {
			write_log(_T("Connect In failed: %08x\n"), hr);
			uninitvideograb();
			return false;
		}
	}

	hr = filterGraph->QueryInterface(IID_IMediaSeeking, (void**)&mediaSeeking);

	hr = filterGraph->QueryInterface(IID_IMediaEvent, (void**)&mediaEvent);

	hr = filterGraph->QueryInterface(IID_IBasicAudio, (void**)&audio);
	setvolumevideograb(100 - currprefs.sound_volume_genlock);

	hr = filterGraph->QueryInterface(IID_IMediaControl, (void**)&mediaControl);
	if (FAILED(hr)) {
		uninitvideograb();
		return false;
	}
	if (mediaSeeking) {
		hr = mediaSeeking->SetTimeFormat(&TIME_FORMAT_FRAME);
		if (FAILED(hr))
			write_log(_T("SetTimeFormat format %08x\n"), hr);
	}
	if (SUCCEEDED(mediaControl->Run())) {
		videoInitialized = true;
		write_log(_T("Playing '%s'\n"), filename ? filename : _T("<capture>"));
		return true;
	} else {
		uninitvideograb();
		return false;
	}
}

uae_s64 getsetpositionvideograb(uae_s64 framepos)
{
	if (!videoInitialized || !mediaSeeking)
		return 0;
	LONGLONG pos;
	HRESULT hr;
	if (framepos < 0) {
		LONGLONG stoppos;
		hr = mediaSeeking->GetPositions(&pos, &stoppos);
		if (FAILED(hr)) {
			write_log(_T("GetPositions failed %08x\n"), hr);
			pos = 0;
		}
		return pos;
	} else {
		LONGLONG pos = framepos;
		hr = mediaSeeking->SetPositions(&pos, AM_SEEKING_AbsolutePositioning, NULL, AM_SEEKING_NoPositioning);
		if (FAILED(hr)) {
			write_log(_T("SetPositions %lld failed %08x\n"), framepos, hr);
		}
		return 0;
	}
}

void setvolumevideograb(int volume)
{
	if (!audio)
		return;
	long vol = log10((float)volume / 100.0) * 4000.0;
	audio->put_Volume(vol);
}

bool getpausevideograb(void)
{
	return videoPaused != 0;
}

void pausevideograb(int pause)
{
	HRESULT hr;
	if (!videoInitialized)
		return;
	if (pause < 0) {
		pause = videoPaused ? 0 : 1;
	}
	if (pause > 0) {
		hr = mediaControl->Pause();
		if (SUCCEEDED(hr))
			videoPaused = true;
	} else if (pause == 0) {
		hr = mediaControl->Run();
		if (SUCCEEDED(hr))
			videoPaused = false;
	}
}

bool getvideograb(long **buffer, int *width, int *height)
{
	HRESULT hr;

	if (!videoInitialized)
		return false;

	// Only need to do this once
	if (!frameBuffer) {
		// The Sample Grabber requires an arbitrary buffer
		// That we only know at runtime.
		// (width * height * 3) bytes will not work.
		hr = sampleGrabber->GetCurrentBuffer(&bufferSize, NULL);
		if (FAILED(hr)) {
			write_log(_T("getvideograb get size %08x\n"), hr);
			return false;
		}
		frameBuffer = new long[bufferSize];
	}

	hr = sampleGrabber->GetCurrentBuffer(&bufferSize, (long*)frameBuffer);
	if (SUCCEEDED(hr)) {
		*buffer = frameBuffer;
		*width = videoWidth;
		*height = videoHeight;
		return true;
	}
	write_log(_T("getvideograb get buffer %08x\n"), hr);
	return false;
}

bool isvideograb(void)
{
	return videoInitialized != 0;
}

void isvideograb_status(void)
{
	if (!videoInitialized)
		return;
	if (currprefs.sound_volume_genlock != changed_prefs.sound_volume_genlock) {
		currprefs.sound_volume_genlock = changed_prefs.sound_volume_genlock;
		setvolumevideograb(100 - currprefs.sound_volume_genlock);
	}
	if (mediaEvent == NULL)
		return;
	long EventCode;
	LONG_PTR lParam1, lParam2;
	for (;;) {
		HRESULT hr = mediaEvent->GetEvent(&EventCode, &lParam1, &lParam2, 0);
		if (FAILED(hr))
			break;
		mediaEvent->FreeEventParams(EventCode, lParam1, lParam2);
		write_log(_T("VIDEOGRAB EVENT %08X %08X %08X\n"), EventCode, lParam1, lParam2);
	}
}
