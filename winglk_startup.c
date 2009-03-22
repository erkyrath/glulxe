/*
   winglk_startup.c: Windows-specific code for the Glulxe interpreter.
*/

#include <windows.h>
#include "glk.h"
#include "gi_blorb.h"
#include "WinGlk.h"

int InitGlk(unsigned int iVersion);

/* Entry point for all Glk applications */
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	/* Turn off Windows errors */
	SetErrorMode(SEM_NOOPENFILEERRORBOX);

	/* Attempt to initialise Glk */
	if (InitGlk(0x00000502) == 0)
		exit(0);

	/* Call the Windows specific initialization routine */
	if (winglk_startup_code() != 0)
	{
		/* Run the application */
		glk_main();

		/* There is no return from this routine */
		glk_exit();
	}

	return 0;
}

extern strid_t gamefile; /* This is defined in glulxe.h */

UINT APIENTRY OFNHookProc(HWND hdlg, UINT uiMsg, WPARAM wParam, LPARAM lParam);

int winglk_startup_code(void)
{
	OPENFILENAME OpenInfo;
	char strFileName[256];
	char Buffer[12];
	int BufferCount;
	char* pszSep;
	char* pszExt;

	winglk_app_set_name("Glulxe");
	winglk_window_set_title("Glulxe Interpreter");

	strFileName[0] = '\0';
	ZeroMemory(&OpenInfo,sizeof(OPENFILENAME));
	OpenInfo.lStructSize = sizeof(OPENFILENAME);
	OpenInfo.lpstrFile = strFileName;
	OpenInfo.nMaxFile = 256;
	OpenInfo.lpstrFilter = "Glulx Files (.blb;.ulx)\0*.blb;*.ulx\0All Files (*.*)\0*.*\0";
	OpenInfo.lpstrTitle = "Select a Glulx game to interpret";
	OpenInfo.Flags = OFN_FILEMUSTEXIST|OFN_HIDEREADONLY|OFN_ENABLEHOOK|OFN_EXPLORER;
	OpenInfo.lpfnHook = OFNHookProc;
	if (GetOpenFileName(&OpenInfo) != 0)
	{
		frefid_t gameref = glk_fileref_create_by_name(fileusage_BinaryMode|fileusage_Data,strFileName,0);
		if (gameref)
		{
			gamefile = glk_stream_open_file(gameref,filemode_Read,0);
			glk_fileref_destroy(gameref);
		}
		else
			return 0;
	}
	else
		return 0;

	/* Examine the loaded file to see what type it is. */
	glk_stream_set_position(gamefile,0,seekmode_Start);
	BufferCount = glk_get_buffer_stream(gamefile,Buffer,12);
	if (BufferCount < 12)
		return 0;

	if (Buffer[0] == 'G' && Buffer[1] == 'l' && Buffer[2] == 'u' && Buffer[3] == 'l')
	{
		if (locate_gamefile(0) == 0)
			return 0;
	}
	else if (Buffer[0] == 'F' && Buffer[1] == 'O' && Buffer[2] == 'R' && Buffer[3] == 'M'
		&& Buffer[8] == 'I' && Buffer[9] == 'F' && Buffer[10] == 'R' && Buffer[11] == 'S')
	{
		if (locate_gamefile(1) == 0)
			return 0;
	}
	else
		return 0;

	/* Set up the resource directory. */
	pszSep = strrchr(strFileName,'\\');
	if (pszSep)
	{
		*pszSep = '\0';
		winglk_set_resource_directory(strFileName);
	}

	return 1;
}

UINT APIENTRY OFNHookProc(HWND hdlg, UINT uiMsg, WPARAM wParam, LPARAM lParam)
{
	if (uiMsg == WM_NOTIFY)
	{
		LPOFNOTIFY pNotify = (LPOFNOTIFY)lParam;
		if (pNotify->hdr.code == CDN_INITDONE)
		{
			HWND hExplorer = GetParent(hdlg);

			RECT ExplorerRect;
			int iWidth = GetSystemMetrics(SM_CXSCREEN);
			int iHeight = GetSystemMetrics(SM_CYSCREEN);
			int iDlgWidth, iDlgHeight;
			int iOffsetX, iOffsetY;

			GetWindowRect(hExplorer,&ExplorerRect);
			iDlgWidth = ExplorerRect.right - ExplorerRect.left;
			iDlgHeight = ExplorerRect.bottom - ExplorerRect.top;
			iOffsetX = (iWidth - iDlgWidth) / 2;
			iOffsetY = (iHeight - iDlgHeight) / 2;

			MoveWindow(hExplorer,iOffsetX,iOffsetY,iDlgWidth,iDlgHeight,1);
			return 1;
		}
	}
	return 0;
}
