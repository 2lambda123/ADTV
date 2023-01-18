#pragma once

#pragma warning(disable:4820)	// padding bytes added in struct

#pragma warning(disable:5045)	// Spectre mitigation if /Qspectre

#ifdef _DEBUG

#define ASSERT(Expression, Message) if (!(Expression)) { MessageBoxW(NULL, Message, L"ASSERTION FAILED", MB_ICONERROR | MB_OK); __ud2(); }

#else

#define ASSERT(Expression, Message) ((void)0);

#endif

#define PRODUCT_VERSION_MAJOR	1

#define PRODUCT_VERSION_MINOR	0

#define REG_SUBKEY_NAME			L"ADTV"

#define MAIN_WINDOW_CLASS_NAME	L"ActiveDirectoryTopologyVisualizerWndClass"

#define MAIN_WINDOW_TITLE		L"Active Directory Topology Visualizer"

#define LOG_FILE_NAME			L"ADTV.log"

#define DEF_FONT_FACE			L"Consolas"

#define SAFE_MODE_RESOLUTION	2

#define TARGET_MICROSECS_PER_FRAME	16667ULL

#define CALCULATE_STATS_EVERY_X_FRAMES	120

#define MIN_CAMERA_ALTITUDE	1

#define MAX_CAMERA_ALTITUDE 100

#define DISCOVERY_IN_PROGRESS_TEXT L"Topology discovery in progress..."

#define DISCOVERY_THREAD_FINISHED WAIT_OBJECT_0

#define DEF_DC_SIZE	256

typedef enum LOGLEVEL
{
	LL_NONE,	// Log nothing

	LL_ERROR,	// Log errors only

	LL_WARN,	// Log warnings and errors

	LL_INFO		// Log everything

} LOGLEVEL;

// LogEvent can log to file, pop a message box, or both.
typedef enum LOGFLAGS
{
	LF_FILE = 1,

	LF_DIALOGBOX = 2

} LOGFLAGS;

typedef struct RESOLUTION
{
	int Width;

	int Height;

} RESOLUTION;

typedef struct REGPARAMS
{
	LOGLEVEL LogLevel;

	DWORD ResolutionIndex;

	wchar_t FontFace[64];

	wchar_t DomainController[64];

} REGPARAMS;

//typedef union PIXEL32 
//{
//	struct Colors 
//	{
//		unsigned char Blue;
//
//		unsigned char Green;
//
//		unsigned char Red;
//
//		unsigned char Alpha;
//
//	} Colors;
//
//	DWORD Bytes;
//
//} PIXEL32;

typedef struct GRAPHICSDATA
{
	HDC ScreenDeviceContext;

	HDC BackBufferDeviceContext;

	HBITMAP BackBufferDIB;

	BITMAPINFO BackBufferBMInfo;

	void* Bits;

	LARGE_INTEGER FrameStart;

	LARGE_INTEGER FrameEnd;

	UINT64 ElapsedMicroseconds;

	UINT64 ElapsedMicrosecondsAccumulatorRaw;

	UINT64 ElapsedMicrosecondsAccumulatorCooked;

	LARGE_INTEGER PerformanceFrequency;

	UINT64 TotalFramesRendered;

	float RawFPSAverage;

	float CookedFPSAverage;

	RECT ClientRect;

	RESOLUTION Resolution;

	HBRUSH MainBrush;

	HPEN Pen;

	HFONT HugeFont;

	HFONT BigFont;

	HFONT SmallFont;

	int EntitiesOnScreen;

	MONITORINFO MonitorInfo;

} GRAPHICSDATA;

typedef struct CAMERA
{
	int x;

	int y;

	int z;

} CAMERA;

typedef enum DC_FLAGS
{
	DCF_GC = 1,

	DCF_RODC = 2,

	DCF_PDCE = 4,

	DCF_SCHEMAMASTER = 8,

	DCF_DOMAINNAMINGMASTER = 16,

	DCF_RIDMASTER = 32,

	DCF_INFRASTRUCTUREMASTER = 64


} DC_FLAGS;

typedef enum ENTITY_TYPE
{
	ET_NONE,

	ET_SITE,

	ET_DC,

	ET_SITELINK,

	ET_TRUST

} ENTITY_TYPE;

typedef struct ENTITY
{
	struct ENTITY* Next;

	ENTITY_TYPE Type;

	int x;

	int y;

	int width;

	int height;
	
	wchar_t name[128];

	wchar_t fqdn[256];

	wchar_t distinguishedname[256];

	wchar_t ntdssettingsdn[256];

	wchar_t site[128];

	DWORD DCsInSite;

	DWORD Flags;

	// bitmap? shape? sitelinks?

} ENTITY;



int WINAPI wWinMain(_In_ HINSTANCE Instance, _In_opt_ HINSTANCE PrevInstance, _In_ PWSTR CmdLine, _In_ int CmdShow);

LRESULT CALLBACK MainWindowProc(_In_ HWND WindowHandle, _In_ UINT Message, _In_ WPARAM WParam, _In_ LPARAM LParam);

DWORD CreateMainWindow(void);

DWORD ReadRegistrySettings(void);

DWORD InitializeGraphics(void);

void LogEventW(_In_ LOGLEVEL Level, _In_ LOGFLAGS Flags, _In_ wchar_t* Message, ...);

void RenderFrameGraphics(void);

DWORD WINAPI DiscoveryThreadProc(_In_ LPVOID lpParameter);

ENTITY* NewEntity(void);

//DWORD Load32BppBitmapFromFile(_In_ wchar_t* FileName, _Inout_ ADTVBITMAP* Bitmap);