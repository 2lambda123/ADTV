// ADTV - Active Directory Topology Visualizer
// Joseph Ryan Ries, 2022-2023
//
// TODO
// -----
// - Fix the mouse zooming. When you scroll the mouse wheel, the camera should zoom in and out. This works, but not that well. It's suboptimal.
// - Site links.. need a smart algorithm to draw lines between sites that represent replication connections, but these lines have to snake around other sites without overlapping.
// - Add more resolutions, but as we increase resolution CPU usage gets much higher and everything gets slower. We already are only drawing entities if they appear on screen,
// but we could speed up even more if we only redraw parts of the screen that have changed. Right now we are clearing the entire backbuffer every frame and that's expensive.


#include <Windows.h>

#include <Windowsx.h>

#include <LM.h>

#include <NtDsAPI.h>

#include <DsGetDC.h>

#include <intrin.h>

#include <stdio.h>

#include "Main.h"

#pragma comment(lib, "Winmm.lib")	// For timeBeginPeriod()

#pragma comment(lib, "Netapi32.lib")

#pragma comment(lib, "Ntdsapi.lib")



HWND gMainWindowHandle;

CRITICAL_SECTION gLogLock;

REGPARAMS gRegParams = { .LogLevel = LL_ERROR };

BOOL gContinue = TRUE;

GRAPHICSDATA gGraphicsData;

CAMERA gCamera = { .x = 0, .y = 0, .z = 1 };
 
ENTITY* gEntities;

BOOL gShouldShowDebugText;

HANDLE gDiscoveryThread;

POINT gMouseScreenPosition;

POINT gMouseWorldPosition;

POINT gMousePreviousCursorPosition;

RESOLUTION gResolutions[] = {
	// 16:9 resolutions, divisible by 8
	{ .Width = 384,  .Height = 216 },
	{ .Width = 512,  .Height = 288 },
	{ .Width = 640,  .Height = 360 },
	{ .Width = 768,  .Height = 432 },
	{ .Width = 896,  .Height = 504 },
	{ .Width = 1024, .Height = 576 },
	{ .Width = 1152, .Height = 648 },
	{ .Width = 1280, .Height = 720 },
	{ .Width = 1408, .Height = 792 },
	{ .Width = 1536, .Height = 864 },
	{ .Width = 1664, .Height = 936 },
	{ .Width = 1792, .Height = 1008 },
	{ .Width = 1920, .Height = 1080 },
	{ .Width = 2048, .Height = 1152 },
	{ .Width = 2176, .Height = 1224 },
	{ .Width = 2304, .Height = 1296 },
	{ .Width = 2432, .Height = 1368 },
	{ .Width = 2560, .Height = 1440 }
};

BOOL gFullscreen;

BOOL gShowHelp = TRUE;

wchar_t* gHelpText = L"H: Dismiss this help text\n"
					 L"F: Fullscreen toggle\n"
	                 L"Arrow keys: pan\n"
					 L"Ctrl+Arrow keys: pan faster\n"
					 L"Home: reset to origin\n"
					 L"Mouse wheel: zoom\n"
					 L"Ctrl+Mouse wheel: zoom faster\n"
					 L"Esc: Quit\n"
					 L"F11: Debug text";



int WINAPI wWinMain(_In_ HINSTANCE Instance, _In_opt_ HINSTANCE PrevInstance, _In_ PWSTR CmdLine, _In_ int CmdShow)
{
	UNREFERENCED_PARAMETER(Instance);

	UNREFERENCED_PARAMETER(PrevInstance);

	UNREFERENCED_PARAMETER(CmdLine);

	UNREFERENCED_PARAMETER(CmdShow);

	MSG WindowMsg = { 0 };	

	if (InitializeCriticalSectionAndSpinCount(&gLogLock, 0x1000) == 0)
	{
		ASSERT(FALSE, L"InitializeCriticalSectionAndSpinCount failed!");
	}	

	if (ReadRegistrySettings() != ERROR_SUCCESS)
	{
		LogEventW(LL_ERROR, LF_DIALOGBOX | LF_FILE, L"[%s] Failed to read registry settings!", __FUNCTIONW__);

		goto Exit;
	}

	gGraphicsData.Resolution = gResolutions[gRegParams.ResolutionIndex];

	if (timeBeginPeriod(1) == TIMERR_NOCANDO)
	{
		LogEventW(LL_ERROR, LF_DIALOGBOX | LF_FILE, L"[%s] Failed to set global timer resolution!", __FUNCTIONW__);

		goto Exit;
	}

	if (CreateMainWindow() != ERROR_SUCCESS)
	{
		LogEventW(LL_ERROR, LF_DIALOGBOX | LF_FILE, L"[%s] Failed to create main window!", __FUNCTIONW__);

		goto Exit;
	}

	if (InitializeGraphics() != ERROR_SUCCESS)
	{
		LogEventW(LL_ERROR, LF_DIALOGBOX | LF_FILE, L"[%s] Failed to initialize graphics!", __FUNCTIONW__);

		goto Exit;
	}

	ASSERT((gGraphicsData.Resolution.Width % 8 == 0) && (gGraphicsData.Resolution.Height % 8 == 0), L"Resolution must be divisible by 8!");

	GetClientRect(gMainWindowHandle, &gGraphicsData.ClientRect);	

	QueryPerformanceFrequency(&gGraphicsData.PerformanceFrequency);

	gDiscoveryThread = CreateThread(
		NULL,
		0,
		DiscoveryThreadProc,
		NULL,
		0,
		NULL);

	if (gDiscoveryThread == NULL)
	{
		LogEventW(LL_ERROR, LF_DIALOGBOX | LF_FILE, L"[%s] Failed to create discovery thread! Error 0x%08lx", __FUNCTIONW__, GetLastError());

		goto Exit;
	}

	LogEventW(LL_INFO, LF_FILE, L"[%s] Entering main message loop.", __FUNCTIONW__);

	while (gContinue)
	{
		QueryPerformanceCounter(&gGraphicsData.FrameStart);

		while (PeekMessageW(&WindowMsg, NULL, 0, 0, PM_REMOVE))
		{
			DispatchMessageW(&WindowMsg);
		}

		RenderFrameGraphics();

		QueryPerformanceCounter(&gGraphicsData.FrameEnd);

		gGraphicsData.ElapsedMicroseconds = gGraphicsData.FrameEnd.QuadPart - gGraphicsData.FrameStart.QuadPart;

		gGraphicsData.ElapsedMicroseconds *= 1000000;

		gGraphicsData.ElapsedMicroseconds /= gGraphicsData.PerformanceFrequency.QuadPart;

		gGraphicsData.TotalFramesRendered++;

		gGraphicsData.ElapsedMicrosecondsAccumulatorRaw += gGraphicsData.ElapsedMicroseconds;

		while (gGraphicsData.ElapsedMicroseconds < TARGET_MICROSECS_PER_FRAME)
		{
			if (gGraphicsData.ElapsedMicroseconds < (TARGET_MICROSECS_PER_FRAME * 0.75f))
			{
				Sleep(1);
			}			

			QueryPerformanceCounter(&gGraphicsData.FrameEnd);

			gGraphicsData.ElapsedMicroseconds = gGraphicsData.FrameEnd.QuadPart - gGraphicsData.FrameStart.QuadPart;

			gGraphicsData.ElapsedMicroseconds *= 1000000;

			gGraphicsData.ElapsedMicroseconds /= gGraphicsData.PerformanceFrequency.QuadPart;
		}

		gGraphicsData.ElapsedMicrosecondsAccumulatorCooked += gGraphicsData.ElapsedMicroseconds;

		if (gGraphicsData.TotalFramesRendered % CALCULATE_STATS_EVERY_X_FRAMES == 0)
		{
			gGraphicsData.RawFPSAverage = 1.0f / (((float)gGraphicsData.ElapsedMicrosecondsAccumulatorRaw / CALCULATE_STATS_EVERY_X_FRAMES) * 0.000001f);

			gGraphicsData.CookedFPSAverage = 1.0f / (((float)gGraphicsData.ElapsedMicrosecondsAccumulatorCooked / CALCULATE_STATS_EVERY_X_FRAMES) * 0.000001f);

			gGraphicsData.ElapsedMicrosecondsAccumulatorRaw = 0;

			gGraphicsData.ElapsedMicrosecondsAccumulatorCooked = 0;
		}
	}

Exit:

	LogEventW(LL_INFO, LF_FILE, L"[%s] Process is exiting.", __FUNCTIONW__);

	LogEventW(LL_INFO, LF_FILE, L"[%s] =================================", __FUNCTIONW__);

	return(0);
}

LRESULT CALLBACK MainWindowProc(_In_ HWND WindowHandle, _In_ UINT Message, _In_ WPARAM WParam, _In_ LPARAM LParam)
{
	LRESULT Result = 0;

	switch (Message)
	{
		case WM_SIZE:
		{
			GetClientRect(gMainWindowHandle, &gGraphicsData.ClientRect);

			break;
		}
		case WM_MOUSEMOVE:
		{
			if (WaitForSingleObject(gDiscoveryThread, 0) == DISCOVERY_THREAD_FINISHED)
			{
				gMouseScreenPosition.x = GET_X_LPARAM(LParam);

				gMouseScreenPosition.y = GET_Y_LPARAM(LParam);

				gMouseWorldPosition.x = (gMouseScreenPosition.x + gCamera.x) * gCamera.z;

				gMouseWorldPosition.y = (gMouseScreenPosition.y + gCamera.y) * gCamera.z;

				// clicking and dragging should pan
				if (WParam & MK_LBUTTON)
				{
					int dx = gMouseScreenPosition.x - gMousePreviousCursorPosition.x;

					int dy = gMouseScreenPosition.y - gMousePreviousCursorPosition.y;

					gCamera.x -= dx;

					gCamera.y -= dy;

					gMousePreviousCursorPosition = gMouseScreenPosition;					
				}
			}

			break;
		}
		case WM_KEYDOWN:
		{
			switch (WParam)
			{
				case VK_ESCAPE:
				{
					gContinue = FALSE;

					break;
				}
				case 0x48: // 'H'
				{
					gShowHelp = !gShowHelp;

					break;
				}
				case 0x46: // 'F'
				{
					if (gFullscreen)
					{
						SetWindowLongPtrW(gMainWindowHandle, GWL_STYLE, WS_VISIBLE | WS_OVERLAPPEDWINDOW);
					}
					else
					{
						SetWindowLongPtrW(gMainWindowHandle, GWL_STYLE, WS_VISIBLE);

						SetWindowPos(gMainWindowHandle, 
							HWND_TOP,
							gGraphicsData.MonitorInfo.rcMonitor.left,
							gGraphicsData.MonitorInfo.rcMonitor.top,
							gGraphicsData.MonitorInfo.rcMonitor.right - gGraphicsData.MonitorInfo.rcMonitor.left,
							gGraphicsData.MonitorInfo.rcMonitor.bottom - gGraphicsData.MonitorInfo.rcMonitor.top, 
							SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
					}

					gFullscreen = !gFullscreen;

					break;
				}
				case VK_HOME:
				{
					gCamera.x = 0;

					gCamera.y = 0;

					break;
				}
				case VK_F11:
				{
					gShouldShowDebugText = !gShouldShowDebugText;

					break;
				}
				case VK_RIGHT:
				{
					if (WaitForSingleObject(gDiscoveryThread, 0) == DISCOVERY_THREAD_FINISHED)
					{
						if (GetKeyState(VK_CONTROL))
						{
							gCamera.x += 10;
						}
						else
						{
							gCamera.x++;
						}
					}

					break;
				}
				case VK_LEFT:
				{
					if (WaitForSingleObject(gDiscoveryThread, 0) == DISCOVERY_THREAD_FINISHED)
					{
						if (GetKeyState(VK_CONTROL))
						{
							gCamera.x -= 10;
						}
						else
						{
							gCamera.x--;
						}
					}

					break;
				}
				case VK_UP:
				{
					if (WaitForSingleObject(gDiscoveryThread, 0) == DISCOVERY_THREAD_FINISHED)
					{
						if (GetKeyState(VK_CONTROL))
						{
							gCamera.y -= 10;
						}
						else
						{
							gCamera.y--;
						}
					}

					break;
				}
				case VK_DOWN:
				{
					if (WaitForSingleObject(gDiscoveryThread, 0) == DISCOVERY_THREAD_FINISHED)
					{
						if (GetKeyState(VK_CONTROL))
						{
							gCamera.y += 10;
						}
						else
						{
							gCamera.y++;
						}
					}

					break;
				}
			}

			break;
		}
		case WM_DESTROY:
		{
			gContinue = FALSE;

			PostQuitMessage(0);

			break;
		}
		case WM_LBUTTONDOWN:
		{
			GetCursorPos(&gMousePreviousCursorPosition);

			ScreenToClient(gMainWindowHandle, &gMousePreviousCursorPosition);

			break;
		}
		case WM_MOUSEWHEEL:
		{
			if (WaitForSingleObject(gDiscoveryThread, 0) == DISCOVERY_THREAD_FINISHED)
			{
				if ((short)HIWORD(WParam) > 0)
				{
					if ((short)LOWORD(WParam) & MK_CONTROL)
					{
						if (gCamera.z > MIN_CAMERA_ALTITUDE + 10)
						{
							gCamera.z -= 10;							
						}
						else
						{
							if (gCamera.z > MIN_CAMERA_ALTITUDE)
							{
								gCamera.z--;								
							}
						}
					}
					else
					{
						if (gCamera.z > MIN_CAMERA_ALTITUDE)
						{
							gCamera.z--;							
						}
					}
				}
				else
				{
					if ((short)LOWORD(WParam) & MK_CONTROL)
					{
						if (gCamera.z < MAX_CAMERA_ALTITUDE - 10)
						{
							gCamera.z += 10;							
						}
						else
						{
							if (gCamera.z < MAX_CAMERA_ALTITUDE)
							{
								gCamera.z++;								
							}
						}
					}
					else
					{
						if (gCamera.z < MAX_CAMERA_ALTITUDE)
						{
							gCamera.z++;							
						}
					}
				}				
				
				POINT WindowCenter = { gGraphicsData.ClientRect.right / 2, gGraphicsData.ClientRect.bottom / 2 };

				gCamera.x = gMouseScreenPosition.x - WindowCenter.x;

				gCamera.y = gMouseScreenPosition.y - WindowCenter.y;
				
			}

			break;
		}
		default:
		{
			Result = DefWindowProcW(WindowHandle, Message, WParam, LParam);

			break;
		}
	}

	return(Result);
}

DWORD CreateMainWindow(void)
{
	DWORD Result = ERROR_SUCCESS;

	RECT WindowRect = { 0 };

	WNDCLASSEXW WndClass = { 0 };

	DWORD WindowStyle = WS_OVERLAPPEDWINDOW | WS_VISIBLE;

	WndClass.cbSize = sizeof(WNDCLASSEXW);

	WndClass.style = 0;

	WndClass.hInstance = GetModuleHandleW(NULL);

	WndClass.lpszClassName = MAIN_WINDOW_CLASS_NAME;

	WndClass.cbClsExtra = 0;

	WndClass.cbWndExtra = 0;

	WndClass.hbrBackground = GetStockObject(BLACK_BRUSH);

	//WndClass.hIcon = 

	//WndClass.hIconSm = 

	WndClass.hCursor = LoadCursorW(NULL, IDC_ARROW);

	//WndClass.lpszMenuName

	WndClass.lpfnWndProc = MainWindowProc;

	if (RegisterClassExW(&WndClass) == 0)
	{
		Result = GetLastError();

		LogEventW(LL_ERROR, LF_FILE, L"[%s] RegisterClassExW failed with error code 0x%08lx!", __FUNCTIONW__, Result);

		goto Exit;
	}	

	// the idea here is to create a window with the exact dimensions such that the client area is exactly 640x480 or whatever, adjusted for window border size

	SetRect(&WindowRect, 0, 0, gGraphicsData.Resolution.Width, gGraphicsData.Resolution.Height);

	AdjustWindowRect(&WindowRect, WindowStyle, FALSE);

	gMainWindowHandle = CreateWindowExW(
		0,
		WndClass.lpszClassName,
		MAIN_WINDOW_TITLE,
		WindowStyle,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		WindowRect.right - WindowRect.left,
		WindowRect.bottom - WindowRect.top,
		NULL,
		NULL,
		GetModuleHandleW(NULL),
		NULL);

	if (gMainWindowHandle == NULL)
	{
		Result = GetLastError();

		LogEventW(LL_ERROR, LF_FILE, L"[%s] CreateWindowEx failed with error code 0x%08lx!", __FUNCTIONW__, Result);

		goto Exit;
	}	

Exit:

	return(Result);
}

DWORD ReadRegistrySettings(void)
{
	DWORD Result = ERROR_SUCCESS;

	HKEY RegKey = NULL;

	DWORD RegDisposition = 0;

	DWORD RegBytesRead = sizeof(DWORD);

	Result = RegCreateKeyExW(HKEY_CURRENT_USER, L"SOFTWARE\\" REG_SUBKEY_NAME, 0, NULL, 0, KEY_READ, NULL, &RegKey, &RegDisposition);

	if (Result != ERROR_SUCCESS)
	{
		LogEventW(LL_ERROR, LF_FILE, L"[%s] RegCreateKeyExW failed with error code 0x%08lx!", __FUNCTIONW__, Result);

		goto Exit;
	}

	if (RegDisposition == REG_CREATED_NEW_KEY)
	{
		LogEventW(LL_INFO, LF_FILE, L"[%s] Registry key did not exist; created new key HKCU\\SOFTWARE\\%s.", __FUNCTIONW__, REG_SUBKEY_NAME);
	}
	else
	{
		LogEventW(LL_INFO, LF_FILE, L"[%s] Registry key did not exist; created new key HKCU\\SOFTWARE\\%s.", __FUNCTIONW__, REG_SUBKEY_NAME);
	}

	////////////////////////////////////////////////////////////////

	Result = RegGetValueW(RegKey, NULL, L"LogLevel", RRF_RT_DWORD, NULL, &gRegParams.LogLevel, &RegBytesRead);

	if (Result != ERROR_SUCCESS)
	{
		if (Result == ERROR_FILE_NOT_FOUND)
		{
			Result = ERROR_SUCCESS;

			LogEventW(LL_INFO, LF_FILE, L"[%s] Registry value '%s' not found. Using default of %d.", __FUNCTIONW__, L"LogLevel", LL_ERROR);

			gRegParams.LogLevel = LL_ERROR;
		}
		else
		{
			LogEventW(LL_ERROR, LF_FILE, L"[%s] Failed to read the '%s' registry value! Error 0x%08lx!", __FUNCTIONW__, L"LogLevel", Result);

			goto Exit;
		}
	}

	LogEventW(LL_INFO, LF_FILE, L"[%s] %s = %d.", __FUNCTIONW__, L"LogLevel", gRegParams.LogLevel);

	////////////////////////////////////////////////////////////////

	Result = RegGetValueW(RegKey, NULL, L"ResolutionIndex", RRF_RT_DWORD, NULL, &gRegParams.ResolutionIndex, &RegBytesRead);

	if (Result != ERROR_SUCCESS)
	{
		if (Result == ERROR_FILE_NOT_FOUND)
		{
			Result = ERROR_SUCCESS;			

			gGraphicsData.MonitorInfo.cbSize = sizeof(MONITORINFO);

			LogEventW(LL_INFO, LF_FILE, L"[%s] Registry value '%s' not found. Attempting to automatically determine the appropriate resolution based on the user's monitor size.", __FUNCTIONW__, L"ResolutionIndex");

			// the primary monitor by definition always has its upper left point at (0,0)
			if (GetMonitorInfoW(MonitorFromWindow(GetDesktopWindow(), MONITOR_DEFAULTTOPRIMARY), &gGraphicsData.MonitorInfo) == 0)
			{
				LogEventW(LL_ERROR, LF_FILE, L"[%s] GetMonitorInfoW failed! Will use a safe default resolution.", __FUNCTIONW__);
				
				gRegParams.ResolutionIndex = SAFE_MODE_RESOLUTION;
			}
			else
			{
				int MonitorWidth = gGraphicsData.MonitorInfo.rcMonitor.right;

				int MonitorHeight = gGraphicsData.MonitorInfo.rcMonitor.bottom;

				LogEventW(LL_INFO, LF_FILE, L"[%s] Primary monitor size: %dx%d.", __FUNCTIONW__, MonitorWidth, MonitorHeight);

				for (int res = 0; res < _countof(gResolutions); res++)
				{
					if ((gResolutions[res].Width >= MonitorWidth) || (gResolutions[res].Height >= MonitorHeight))
					{
						if (gResolutions[res].Width == MonitorWidth && gResolutions[res].Height == MonitorHeight)
						{
							LogEventW(LL_INFO, LF_FILE, L"[%s] Based on primary monitor size, found an optimal resolution of %dx%d.",
								__FUNCTIONW__,
								gResolutions[res].Width,
								gResolutions[res].Height);

							gRegParams.ResolutionIndex = res;
						}
						else
						{
							LogEventW(LL_INFO, LF_FILE, L"[%s] Based on primary monitor size, found an optimal resolution of %dx%d.",
								__FUNCTIONW__,
								(res > 0) ? gResolutions[res - 1].Width : gResolutions[res].Width,
								(res > 0) ? gResolutions[res - 1].Height : gResolutions[res].Height);

							gRegParams.ResolutionIndex = (res > 0) ? res - 1 : res;
						}

						break;						
					}
				}
			}
		}
		else
		{
			LogEventW(LL_ERROR, LF_FILE, L"[%s] Failed to read the '%s' registry value! Error 0x%08lx!", __FUNCTIONW__, L"ResolutionIndex", Result);

			goto Exit;
		}
	}

	LogEventW(LL_INFO, LF_FILE, L"[%s] %s = %d.", __FUNCTIONW__, L"ResolutionIndex", gRegParams.ResolutionIndex);

	////////////////////////////////////////////////////////////////

	RegBytesRead = sizeof(gRegParams.FontFace);

	Result = RegGetValueW(RegKey, NULL, L"FontFace", RRF_RT_REG_SZ, NULL, &gRegParams.FontFace, &RegBytesRead);

	if (Result != ERROR_SUCCESS)
	{
		if (Result == ERROR_FILE_NOT_FOUND)
		{
			Result = ERROR_SUCCESS;

			LogEventW(LL_INFO, LF_FILE, L"[%s] Registry value '%s' not found. Using default of %s.", __FUNCTIONW__, L"FontFace", DEF_FONT_FACE);

			wcscpy_s(gRegParams.FontFace, _countof(gRegParams.FontFace), DEF_FONT_FACE);
		}
		else
		{
			LogEventW(LL_ERROR, LF_FILE, L"[%s] Failed to read the '%s' registry value! Error 0x%08lx!", __FUNCTIONW__, L"FontFace", Result);

			goto Exit;
		}
	}

	LogEventW(LL_INFO, LF_FILE, L"[%s] %s = %s.", __FUNCTIONW__, L"FontFace", gRegParams.FontFace);

	////////////////////////////////////////////////////////////////

	RegBytesRead = sizeof(gRegParams.DomainController);

	Result = RegGetValueW(RegKey, NULL, L"DomainController", RRF_RT_REG_SZ, NULL, &gRegParams.DomainController, &RegBytesRead);

	if (Result != ERROR_SUCCESS)
	{
		if (Result == ERROR_FILE_NOT_FOUND)
		{
			Result = ERROR_SUCCESS;

			LogEventW(LL_INFO, LF_FILE, L"[%s] Registry value '%s' not found. Will attempt to automatically locate a DC.", __FUNCTIONW__, L"DomainController");			
		}
		else
		{
			LogEventW(LL_ERROR, LF_FILE, L"[%s] Failed to read the '%s' registry value! Error 0x%08lx!", __FUNCTIONW__, L"DomainController", Result);

			goto Exit;
		}
	}

	LogEventW(LL_INFO, LF_FILE, L"[%s] %s = %s.", __FUNCTIONW__, L"DomainController", wcslen(gRegParams.DomainController) ? gRegParams.DomainController : L"(null)");

Exit:

	return(Result);
}

void LogEventW(_In_ LOGLEVEL Level, _In_ LOGFLAGS Flags, _In_ wchar_t* Message, ...)
{
	va_list Args = NULL;

	size_t MsgLen = wcslen(Message);

	SYSTEMTIME Time = { 0 };

	HANDLE FileHandle = INVALID_HANDLE_VALUE;

	DWORD EndOfFile = 0;

	DWORD BytesWritten = 0;

	wchar_t DateTimeString[64] = { 0 };

	wchar_t SeverityString[32] = { 0 };

	wchar_t FormattedMessage[1024] = { 0 };

	wchar_t FinalMessage[1280] = { 0 };

	if (MsgLen < 1 || MsgLen >= 1024)
	{
		ASSERT(FALSE, L"LogEventW: Message was either too short or too long!");
	}

	if (!(Flags & LF_FILE) && !(Flags & LF_DIALOGBOX))
	{
		ASSERT(FALSE, L"LogEventW: Must supply some flag!")
	}

	if (gRegParams.LogLevel < Level)
	{
		return;
	}

	switch (Level)
	{
		case LL_NONE:
		{
			return;
		}
		case LL_INFO:
		{
			wcscpy_s(SeverityString, _countof(SeverityString), L"[INFO]");

			break;
		}
		case LL_WARN:
		{
			wcscpy_s(SeverityString, _countof(SeverityString), L"[WARN]");

			break;
		}
		case LL_ERROR:
		{
			wcscpy_s(SeverityString, _countof(SeverityString), L"[ERROR]");

			break;
		}
		default:
		{
			ASSERT(FALSE, L"LogEventW: Unrecognized log level!");
		}
	}

	GetLocalTime(&Time);

	va_start(Args, Message);

	_vsnwprintf_s(FormattedMessage, _countof(FormattedMessage), _TRUNCATE, Message, Args);

	va_end(Args);

	_snwprintf_s(DateTimeString, _countof(DateTimeString), _TRUNCATE, L"\n[%02u/%02u/%u %02u:%02u:%02u.%03u]", Time.wMonth, Time.wDay, Time.wYear, Time.wHour, Time.wMinute, Time.wSecond, Time.wMilliseconds);

	_snwprintf_s(FinalMessage, _countof(FinalMessage), _TRUNCATE, L"%s%s%s", DateTimeString, SeverityString, FormattedMessage);

	if (Flags & LF_FILE)
	{
		EnterCriticalSection(&gLogLock);

		if ((FileHandle = CreateFileW(LOG_FILE_NAME, FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL)) == INVALID_HANDLE_VALUE)
		{
			ASSERT(FALSE, L"Failed to access log file!");
		}

		EndOfFile = SetFilePointer(FileHandle, 0, NULL, FILE_END);

		if (EndOfFile == 0)
		{
			// If this is the beginning of a new file we need to write the utf16 BOM.

			unsigned char BOM[2] = { 0xFF, 0xFE };

			WriteFile(FileHandle, BOM, 2, &BytesWritten, NULL);
		}

		WriteFile(FileHandle, FinalMessage, (DWORD)wcslen(FinalMessage) * sizeof(wchar_t), &BytesWritten, NULL);

		if (FileHandle)
		{
			CloseHandle(FileHandle);
		}

		LeaveCriticalSection(&gLogLock);
	}

	if (Flags & LF_DIALOGBOX)
	{
		switch (Level)
		{
			case LL_NONE:
			{
				ASSERT(FALSE, L"LogEventW: Level 0 not valid!");
			}
			case LL_INFO:
			{
				MessageBoxW(gMainWindowHandle, FinalMessage, SeverityString, MB_OK | MB_ICONINFORMATION);

				break;
			}
			case LL_WARN:
			{
				MessageBoxW(gMainWindowHandle, FinalMessage, SeverityString, MB_OK | MB_ICONWARNING);

				break;
			}
			case LL_ERROR:
			{
				MessageBoxW(gMainWindowHandle, FinalMessage, SeverityString, MB_OK | MB_ICONERROR);

				break;
			}
			default:
			{
				ASSERT(FALSE, L"LogEventW: Unrecognized log level!");
			}
		}
	}
}

void RenderFrameGraphics(void)
{
	memset(gGraphicsData.Bits, 0, (UINT64)gGraphicsData.Resolution.Width * (UINT64)gGraphicsData.Resolution.Height * (32 / 8));	

	if (WaitForSingleObject(gDiscoveryThread, 0) == DISCOVERY_THREAD_FINISHED && gEntities)
	{
		ENTITY* Current = gEntities;

		while (Current->Next != NULL)
		{
			RECT EntityRect = { 0 };

			SIZE TextSize = { 0 };

			SetRect(
				&EntityRect,
				(Current->x * (1.0f / gCamera.z)) - gCamera.x,
				(Current->y * (1.0f / gCamera.z)) - gCamera.y,
				(Current->x * (1.0f / gCamera.z)) + Current->width * (1.0f / gCamera.z) - gCamera.x,
				(Current->y * (1.0f / gCamera.z)) + Current->height * (1.0f / gCamera.z) - gCamera.y);

			if (EntityRect.left > gGraphicsData.ClientRect.right ||
				EntityRect.top > gGraphicsData.ClientRect.bottom ||
				EntityRect.bottom < gGraphicsData.ClientRect.top ||
				EntityRect.right < 0)
			{
				Current = Current->Next;

				continue;
			}	


			if (Current->Type == ET_SITE)			
			{
				// Rectangles... you would think you only need 4 verticies to draw a rectangle		
				// but the first vertex needs to be the starting point apparently		
				POINT Verticies[] = { 			
					{ EntityRect.left,  EntityRect.top },			
					{ EntityRect.right, EntityRect.top },			
					{ EntityRect.right, EntityRect.bottom },			
					{ EntityRect.left,  EntityRect.bottom },			
					{ EntityRect.left,  EntityRect.top }		
				};
		
				// FrameRect is always 1 pixel thick, but Polyline uses the currently selected PEN and can be any thickness we want.
		
				Polyline(gGraphicsData.BackBufferDeviceContext, Verticies, _countof(Verticies));

				// Draw the site name on the top and bottom of the site rectangle
				// The more we are zoomed in, the larger the text is
				// if zoomed far enough out, don't draw it at all

				switch (gCamera.z)
				{
					case 1:
					{
						SelectObject(gGraphicsData.BackBufferDeviceContext, gGraphicsData.HugeFont);

						break;
					}
					case 2:
					case 3:
					{
						SelectObject(gGraphicsData.BackBufferDeviceContext, gGraphicsData.BigFont);

						break;
					}
					case 4:
					case 5:
					{
						SelectObject(gGraphicsData.BackBufferDeviceContext, gGraphicsData.SmallFont);

						break;
					}
				}

				if (gCamera.z < 6)
				{
					GetTextExtentPoint32W(gGraphicsData.BackBufferDeviceContext, Current->name, (int)wcslen(Current->name), &TextSize);

					TextOutW(
						gGraphicsData.BackBufferDeviceContext,
						((Current->x * (1.0f / gCamera.z)) + Current->width * (1.0f / gCamera.z) / 2) - (TextSize.cx / 2) - gCamera.x,
						(Current->y * (1.0f / gCamera.z)) + Current->height * (1.0f / gCamera.z) - gCamera.y,
						Current->name,
						(int)wcslen(Current->name));

					TextOutW(
						gGraphicsData.BackBufferDeviceContext,
						((Current->x * (1.0f / gCamera.z)) + Current->width * (1.0f / gCamera.z) / 2) - (TextSize.cx / 2) - gCamera.x,
						(Current->y * (1.0f / gCamera.z)) - (TextSize.cy * gCamera.z) * (1.0f / gCamera.z) - gCamera.y,
						Current->name,
						(int)wcslen(Current->name));
				}
			}
			else if (Current->Type == ET_DC)
			{
						
				// Triangles
				POINT Verticies[] = { 
					{ EntityRect.left, EntityRect.bottom }, 
					{ EntityRect.right, EntityRect.bottom },
					{ EntityRect.right - ((EntityRect.right - EntityRect.left) / 2), EntityRect.top}};

				Polygon(gGraphicsData.BackBufferDeviceContext, Verticies, _countof(Verticies));						

				switch (gCamera.z)
				{
					case 1:
					{
						SelectObject(gGraphicsData.BackBufferDeviceContext, gGraphicsData.HugeFont);
						
						break;
					}
					case 2:	
					{
						SelectObject(gGraphicsData.BackBufferDeviceContext, gGraphicsData.BigFont);
						
						break;
					}
					case 3:
					{
						SelectObject(gGraphicsData.BackBufferDeviceContext, gGraphicsData.SmallFont);

						break;
					}
				}

				if (gCamera.z < 4)
				{
					GetTextExtentPoint32W(gGraphicsData.BackBufferDeviceContext, Current->name, (int)wcslen(Current->name), &TextSize);
					
					TextOutW(
						gGraphicsData.BackBufferDeviceContext,
						((Current->x * (1.0f / gCamera.z)) + Current->width * (1.0f / gCamera.z)) - gCamera.x,
						(Current->y * (1.0f / gCamera.z)) + ((Current->height / 2) - (TextSize.cy / 2)) * (1.0f / gCamera.z) - gCamera.y,
						Current->fqdn,
						(int)wcslen(Current->fqdn));
				}
			}

			gGraphicsData.EntitiesOnScreen++;

			Current = Current->Next;
		}
	}

	if (WaitForSingleObject(gDiscoveryThread, 0) != DISCOVERY_THREAD_FINISHED)
	{
		SelectObject(gGraphicsData.BackBufferDeviceContext, gGraphicsData.BigFont);

		static int EllipsisAnimation = 3;

		SIZE TextSize;

		RECT Rect;		

		if (gGraphicsData.TotalFramesRendered % 30 == 0)
		{
			EllipsisAnimation--;

			if (EllipsisAnimation < 0)
			{
				EllipsisAnimation = 3;
			}
		}		

		SetRect(&Rect, 64, (gGraphicsData.Resolution.Height / 2) - 32, gGraphicsData.Resolution.Width - 64, (gGraphicsData.Resolution.Height / 2) + 32);

		FrameRect(gGraphicsData.BackBufferDeviceContext, &Rect, gGraphicsData.MainBrush);

		Rect.left += 1;

		Rect.top += 1;

		Rect.right -= 1;

		Rect.bottom -= 1;

		FillRect(gGraphicsData.BackBufferDeviceContext, &Rect, GetStockObject(BLACK_BRUSH));		

		GetTextExtentPoint32W(gGraphicsData.BackBufferDeviceContext, DISCOVERY_IN_PROGRESS_TEXT, (int)wcslen(DISCOVERY_IN_PROGRESS_TEXT), &TextSize);

		TextOutW(gGraphicsData.BackBufferDeviceContext, (gGraphicsData.Resolution.Width / 2) - (TextSize.cx / 2), (gGraphicsData.Resolution.Height / 2) - (TextSize.cy / 2), DISCOVERY_IN_PROGRESS_TEXT, (int)wcslen(DISCOVERY_IN_PROGRESS_TEXT) - EllipsisAnimation);		
	}
	else
	{
		// Discovery thread is done, check its exit code.

		DWORD ExitCode = ERROR_SUCCESS;

		GetExitCodeThread(gDiscoveryThread, &ExitCode);

		if (ExitCode != ERROR_SUCCESS)
		{			
			LogEventW(
				LL_ERROR, 
				LF_DIALOGBOX | LF_FILE, 
				L"[%s] Discovery thread failed with error code 0x%08lx!\nThis tool must be run from a system that is a member of the Active Directory forest you wish to analyze, and it must be able to contact a domain controller. Consider using the 'DomainController' registry setting if automatic DC discovery isn't working.", __FUNCTIONW__, ExitCode);

			gContinue = FALSE;

			return;
		}
	}

	if (gShouldShowDebugText)
	{
		SelectObject(gGraphicsData.BackBufferDeviceContext, gGraphicsData.SmallFont);

		wchar_t debugtext[256];

		_snwprintf_s(
			debugtext,
			_countof(debugtext),
			_TRUNCATE,
			L"FPS:%.1f/%.1f CameraXYZ:%d,%d,%d Res:%dx%d EntitiesOnScreen:%d Mouse:(%ld,%ld) (%ld,%ld)",
			gGraphicsData.RawFPSAverage, 
			gGraphicsData.CookedFPSAverage, 
			gCamera.x, 
			gCamera.y, 
			gCamera.z, 
			gGraphicsData.Resolution.Width,
			gGraphicsData.Resolution.Height, 
			gGraphicsData.EntitiesOnScreen, 
			gMouseScreenPosition.x, 
			gMouseScreenPosition.y,
			gMouseWorldPosition.x,
			gMouseWorldPosition.y);
		
		TextOutW(gGraphicsData.BackBufferDeviceContext, 0, gGraphicsData.Resolution.Height - 36, debugtext, (int)wcslen(debugtext));

		_snwprintf_s(
			debugtext,
			_countof(debugtext),
			_TRUNCATE,
			L"ADTV v%d.%d, © 2022 Joseph Ryan Ries",
			PRODUCT_VERSION_MAJOR, PRODUCT_VERSION_MINOR);

		TextOutW(gGraphicsData.BackBufferDeviceContext, 0, gGraphicsData.Resolution.Height - 18, debugtext, (int)wcslen(debugtext));
	}

	if (gShowHelp)
	{
		SelectObject(gGraphicsData.BackBufferDeviceContext, gGraphicsData.SmallFont);

		DrawTextExW(
			gGraphicsData.BackBufferDeviceContext, 
			gHelpText, 
			(int)wcslen(gHelpText), 
			&(RECT) {.left = 0, .top = 0, .bottom = gGraphicsData.Resolution.Height, .right = gGraphicsData.Resolution.Width }, 0, NULL);		
	}

	StretchDIBits(
		gGraphicsData.ScreenDeviceContext,
		0,
		0,
		gGraphicsData.ClientRect.right - gGraphicsData.ClientRect.left,
		gGraphicsData.ClientRect.bottom - gGraphicsData.ClientRect.top,
		0,
		0,
		gGraphicsData.Resolution.Width,
		gGraphicsData.Resolution.Height,
		gGraphicsData.Bits,
		&gGraphicsData.BackBufferBMInfo,
		DIB_RGB_COLORS,
		SRCCOPY);

	gGraphicsData.EntitiesOnScreen = 0;
}

DWORD InitializeGraphics(void)
{
	DWORD Result = ERROR_SUCCESS;	
	
	gGraphicsData.ScreenDeviceContext = GetDC(gMainWindowHandle);

	gGraphicsData.BackBufferDeviceContext = CreateCompatibleDC(gGraphicsData.ScreenDeviceContext);

	if (gGraphicsData.ScreenDeviceContext == NULL || gGraphicsData.BackBufferDeviceContext == NULL)
	{
		Result = GetLastError();

		LogEventW(LL_ERROR, LF_FILE, L"[%s] Graphics initialization failed with 0x%08lx!", __FUNCTIONW__, Result);

		goto Exit;
	}

	SetBkMode(gGraphicsData.BackBufferDeviceContext, TRANSPARENT);

	gGraphicsData.BackBufferBMInfo.bmiHeader.biSize = sizeof(gGraphicsData.BackBufferBMInfo.bmiHeader);

	gGraphicsData.BackBufferBMInfo.bmiHeader.biWidth = gGraphicsData.Resolution.Width;

	gGraphicsData.BackBufferBMInfo.bmiHeader.biHeight = gGraphicsData.Resolution.Height;

	gGraphicsData.BackBufferBMInfo.bmiHeader.biBitCount = 32;

	gGraphicsData.BackBufferBMInfo.bmiHeader.biCompression = BI_RGB;

	gGraphicsData.BackBufferBMInfo.bmiHeader.biPlanes = 1;

	gGraphicsData.BackBufferDIB = CreateDIBSection(gGraphicsData.ScreenDeviceContext, &gGraphicsData.BackBufferBMInfo, DIB_RGB_COLORS, &gGraphicsData.Bits, NULL, 0);

	if (gGraphicsData.BackBufferDIB == NULL || gGraphicsData.Bits == NULL)
	{
		Result = GetLastError();

		LogEventW(LL_ERROR, LF_FILE, L"[%s] Graphics initialization failed with 0x%08lx!", __FUNCTIONW__, Result);		

		goto Exit;
	}

	SelectObject(gGraphicsData.BackBufferDeviceContext, gGraphicsData.BackBufferDIB);

	gGraphicsData.MainBrush = CreateSolidBrush(RGB(255, 255, 255));

	//gGraphicsData.InactiveBrush = CreateSolidBrush(RGB(96, 96, 96));

	gGraphicsData.Pen = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));

	SelectObject(gGraphicsData.BackBufferDeviceContext, gGraphicsData.Pen);

	gGraphicsData.HugeFont = CreateFontW(60, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, gRegParams.FontFace);

	gGraphicsData.BigFont = CreateFontW(36, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, gRegParams.FontFace);

	gGraphicsData.SmallFont = CreateFontW(18, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, gRegParams.FontFace);

	SelectObject(gGraphicsData.BackBufferDeviceContext, gGraphicsData.BigFont);

	SetTextColor(gGraphicsData.BackBufferDeviceContext, RGB(255, 255, 255));

Exit:

	return(Result);
}

DWORD WINAPI DiscoveryThreadProc(_In_ LPVOID lpParameter)
{
	UNREFERENCED_PARAMETER(lpParameter);

	DWORD Result = ERROR_SUCCESS;

	wchar_t WindowText[128] = { 0 };

	DOMAIN_CONTROLLER_INFOW* DCLocatorInfo = NULL;

	HANDLE DSBindHandle = NULL;	

	DS_NAME_RESULTW* Sites = NULL;

	DS_DOMAIN_TRUSTSW* Trusts = NULL;

	ULONG TrustCount = 0;

	ENTITY* Current = NULL;

	LogEventW(LL_INFO, LF_FILE, L"[%s] Discovery thread beginning.", __FUNCTIONW__);

	// First find our initial DC... the rest of the discovery of the entire forest has to begin somewhere... we don't know yet
	// whether we are joined to the forest root domain or a child domain of it. Azure AD/Hybrid joined systems don't work with DCLocator
	// as far as I know, in which case you have to give the app a hint by populating the DomainController registry setting with an initial DC to contact.
	// If the user has not specified DomainController, it will be NULL, which should work fine for traditional AD-joined systems.

	if ((Result = DsGetDcNameW(gRegParams.DomainController, NULL, NULL, NULL, DS_GC_SERVER_REQUIRED, &DCLocatorInfo)) != ERROR_SUCCESS)
	{		
		LogEventW(LL_ERROR, LF_FILE, L"[%s] DsGetDcNameW failed with 0x%08lx!", __FUNCTIONW__, Result);

		goto Exit;
	}

	LogEventW(LL_INFO, LF_FILE, L"[%s] DsGetDcNameW found DC %s. Domain %s. Forest %s.", 
		__FUNCTIONW__, 
		DCLocatorInfo->DomainControllerName,
		DCLocatorInfo->DomainName,
		DCLocatorInfo->DnsForestName);

	GetWindowTextW(gMainWindowHandle, WindowText, _countof(WindowText));

	wcscat_s(WindowText, _countof(WindowText), L" - ");

	wcscat_s(WindowText, _countof(WindowText), DCLocatorInfo->DnsForestName);

	SetWindowTextW(gMainWindowHandle, WindowText);

	// Since most of the info we need will come from the configuration NC, which is forest-wide, it doesn't matter right now whether we're talking to a 
	// forest root DC or a child domain DC.

	if ((Result = DsBindW(DCLocatorInfo->DomainControllerName, NULL, &DSBindHandle)) != ERROR_SUCCESS)
	{
		LogEventW(LL_ERROR, LF_FILE, L"[%s] DsBindW failed with 0x%08lx!", __FUNCTIONW__, Result);

		goto Exit;
	}

	LogEventW(LL_INFO, LF_FILE, L"[%s] Successfully bound to DC.", __FUNCTIONW__);

	if ((Result = DsEnumerateDomainTrustsW(DCLocatorInfo->DomainControllerName, DS_DOMAIN_TREE_ROOT | DS_DOMAIN_IN_FOREST, &Trusts, &TrustCount)) != ERROR_SUCCESS)
	{
		LogEventW(LL_ERROR, LF_FILE, L"[%s] DsEnumerateDomainTrustsW failed with 0x%08lx!", __FUNCTIONW__, Result);

		goto Exit;
	}

	LogEventW(LL_INFO, LF_FILE, L"[%s] Found %d trusts in forest root %s.", __FUNCTIONW__, TrustCount, DCLocatorInfo->DnsForestName);

	for (unsigned int trust = 0; trust < TrustCount; trust++)
	{
		ENTITY* New = NewEntity();

		New->Type = ET_TRUST;

		wcscpy_s(New->name, _countof(New->name), Trusts[trust].DnsDomainName);

		wcscpy_s(New->fqdn, _countof(New->fqdn), Trusts[trust].DnsDomainName);

		New->Flags = Trusts[trust].Flags;
	}

	if ((Result = DsListSitesW(DSBindHandle, &Sites)) != NO_ERROR)
	{
		LogEventW(LL_ERROR, LF_FILE, L"[%s] DsListSitesW failed with 0x%08lx!", __FUNCTIONW__, Result);

		goto Exit;
	}

	LogEventW(LL_INFO, LF_FILE, L"[%s] Found %d sites.", __FUNCTIONW__, Sites->cItems);

	for (unsigned int site = 0; site < Sites->cItems; site++)
	{
		if (Sites->rItems[site].status != NO_ERROR)
		{
			Result = Sites->rItems[site].status;

			LogEventW(LL_ERROR, LF_FILE, L"[%s] Site %d reports error 0x%08lx!", __FUNCTIONW__, site, Result);

			goto Exit;
		}

		// Too verbose	//LogEventW(LL_INFO, LF_FILE, L"[%s] Found site %s.", __FUNCTIONW__, Sites->rItems[site].pName);

		ENTITY* New = NewEntity();

		wchar_t CommonName[64] = { 0 };

		New->Type = ET_SITE;

		wcscpy_s(New->distinguishedname, _countof(New->distinguishedname), Sites->rItems[site].pName);

		// we have a dn now like cn=sitename,cn=sites,dc=configuration,dc=domain,dc=com or whatever
		// I just want to extract the cn from that

		for (int character = 3; character < _countof(CommonName) - 1; character++)
		{
			if (New->distinguishedname[character] == L',')
			{
				break;
			}

			CommonName[character - 3] = New->distinguishedname[character];
		}

		wcscpy_s(New->name, _countof(New->name), CommonName);		
	}

	Current = gEntities;

	while (Current->Next != NULL)
	{
		if (Current->Type == ET_SITE)
		{
			DS_NAME_RESULTW* ServersInSite = NULL;			

			if ((Result = DsListServersInSiteW(DSBindHandle, Current->distinguishedname, &ServersInSite)) != NO_ERROR)
			{
				LogEventW(LL_ERROR, LF_FILE, L"[%s] DsListServersInSiteW reports error 0x%08lx!", __FUNCTIONW__, Result);

				goto Exit;
			}

			for (unsigned int dc = 0; dc < ServersInSite->cItems; dc++)
			{
				DS_NAME_RESULTW* DCInfo = NULL;

				if (ServersInSite->rItems[dc].status != NO_ERROR)
				{
					Result = ServersInSite->rItems[dc].status;

					LogEventW(LL_ERROR, LF_FILE, L"[%s] DsListServersInSiteW reports error 0x%08lx!", __FUNCTIONW__, Result);

					goto Exit;
				}

				ENTITY* New = NewEntity();

				New->Type = ET_DC;

				wcscpy_s(New->distinguishedname, _countof(New->distinguishedname), ServersInSite->rItems[dc].pName);

				wcscpy_s(New->site, _countof(New->site), Current->distinguishedname);

				// we currently only have the distinguishedname, now to get the dns fqdn of this dc

				if ((Result = DsListInfoForServerW(DSBindHandle, ServersInSite->rItems[dc].pName, &DCInfo)) != NO_ERROR)
				{
					LogEventW(LL_ERROR, LF_FILE, L"[%s] DsListInfoForServerW reports error 0x%08lx!", __FUNCTIONW__, Result);

					goto Exit;
				}

				wcscpy_s(New->fqdn, _countof(New->fqdn), DCInfo->rItems[DS_LIST_DNS_HOST_NAME_FOR_SERVER].pName);

				if (DCInfo)
				{
					DsFreeNameResultW(DCInfo);
				}

				Current->DCsInSite++;
			}

			LogEventW(LL_INFO, LF_FILE, L"[%s] %d DCs found in site %s.", __FUNCTIONW__, Current->DCsInSite, Current->name);

			if (ServersInSite)
			{
				DsFreeNameResultW(ServersInSite);
			}
		}

		Current = Current->Next;
	}

	//// position the sites
	//// then position the dcs within the sites

	POINT PreviousSite = { .x = -64, .y = 64 };

	int PreviousSiteWidth = 0;

	Current = gEntities;	

	while (Current->Next != NULL)
	{
		if (Current->Type == ET_SITE)
		{			
			Current->x = PreviousSite.x + PreviousSiteWidth + 256;

			Current->y = PreviousSite.y;

			// calculate width of the site based on the largest text size of the dc names
			ENTITY* DC = gEntities;

			int DCindex = 0;

			while (DC->Next != NULL)
			{
				SelectObject(gGraphicsData.BackBufferDeviceContext, gGraphicsData.HugeFont);

				if ((DC->Type == ET_DC) && (_wcsicmp(DC->site, Current->distinguishedname) == 0))
				{
					// this dc is in this site

					SIZE TextSize;

					GetTextExtentPointW(gGraphicsData.BackBufferDeviceContext, DC->fqdn, (int)wcslen(DC->fqdn), &TextSize);
	
					// expand the width of the site if necessary
					if (TextSize.cx > Current->width)	
					{							
						Current->width = TextSize.cx;
					}
								
					DC->x = Current->x + (DEF_DC_SIZE / 4);
			
					DC->y = (Current->y + (DEF_DC_SIZE / 4)) + (DCindex * (DEF_DC_SIZE + (DEF_DC_SIZE / 2)));
			
					DC->width = DEF_DC_SIZE;
			
					DC->height = DEF_DC_SIZE;

					DCindex++;
				}				

				DC = DC->Next;
			}
				

			// add extra width for the triangle DC icons, and some padding

			Current->width += DEF_DC_SIZE + (DEF_DC_SIZE / 2);

			if (Current->DCsInSite)
			{
				Current->height = (Current->DCsInSite * DEF_DC_SIZE) + (Current->DCsInSite * (DEF_DC_SIZE / 2));
			}
			else
			{
				Current->height = DEF_DC_SIZE; // an empty site with no dcs in it
			}


			PreviousSite.x = Current->x;

			PreviousSite.y = Current->y;

			PreviousSiteWidth = Current->width;
		}

		Current = Current->Next;
	}

	Current = gEntities;

	while (Current->Next != NULL)
	{


		Current = Current->Next;
	}

	

Exit:

	if (Trusts)
	{
		NetApiBufferFree(Trusts);
	}

	if (DCLocatorInfo)
	{
		NetApiBufferFree(DCLocatorInfo);
	}

	if (Sites)
	{
		DsFreeNameResultW(Sites);
	}

	if (DSBindHandle)
	{
		DsUnBindW(&DSBindHandle);
	}

	LogEventW(LL_INFO, LF_FILE, L"[%s] Discovery thread ending.", __FUNCTIONW__);

	return(Result);
}

ENTITY* NewEntity(void)
{
	if (gEntities == NULL)
	{
		// First run
		gEntities = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(ENTITY));

		return(gEntities);
	}
	else
	{
		ENTITY* Current = gEntities;

		if (Current->Next == NULL)
		{
			// Second run
			Current->Next = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(ENTITY));

			return(Current->Next);
		}

		while (Current->Next != NULL)
		{
			// 3rd+ run
			Current = Current->Next;
		}

		Current->Next = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(ENTITY));

		return(Current->Next);
	}
}