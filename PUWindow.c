#include <windows.h>
#include <commctrl.h>
#include "resource/PUWindow.h"

/*
differences between this and original Pickup Window 1.9b by 545 Studios (c) 2001
- free and open source codes
- less size (15,872 vs 124,928 - almost 90% reduced)
- no additional files required like "MSVBVM50.DLL" to run under modern systems
- works under Windows 9x (dynamic load for SetLayeredWindowAttributes() function)
- crosshair cursor for easy aiming instead icons with unknown hotspot location
- redesign interface to be more compact and comfortable
- pixel color for semi-transparent windows can be captured on NT 5+
- GDI handles do not leak (check Task Manager with GDI handles column shown)
*/

// get window text
TCHAR *GetWndText(HWND wnd) {
TCHAR *result;
LONG sz;
  result = NULL;
  if (wnd) {
    sz = SendMessage(wnd, WM_GETTEXTLENGTH, 0, 0);
    if (sz > 0) {
      sz++;
      result = (TCHAR *) LocalAlloc(LPTR, sz * sizeof(result[0]));
      if (result) {
        SendMessage(wnd, WM_GETTEXT, (WPARAM) sz, (LPARAM) result);
        result[sz - 1] = 0;
      }
    }
  }
  return(result);
}

#define GetWndData(w,i) GetWindowLong((i) ? GetDlgItem((w), (i)) : (w), GWL_USERDATA)
#define SetWndData(w,i,l) SetWindowLong((i) ? GetDlgItem((w), (i)) : (w), GWL_USERDATA, (LONG) (l))

// #if(WINVER >= 0x0500)
#ifndef CAPTUREBLT
// Include layered windows
#define CAPTUREBLT ((DWORD) 0x40000000)
#endif

void ZoomAreaInit(HWND wnd, BOOL reinit) {
HBITMAP hb;
RECT rc;
DWORD i;
HDC hc;
  hc = (HDC) GetWndData(wnd, IDC_ZRGN);
  // free if already allocated
  if (hc) {
    DeleteDC(hc);
    hc = 0;
  }
  // allocate requested
  if (reinit) {
    hc = CreateCompatibleDC(0);
    if (hc) {
      i = GetWndData(wnd, IDC_ZOOM);
      // double size for resize
      hb = CreateBitmap(i, i * 2, GetDeviceCaps(hc, PLANES), GetDeviceCaps(hc, BITSPIXEL), NULL);
      if (hb) {
        DeleteObject(SelectObject(hc, hb));
        // Windows 98: remove junk bitmap data bits
        ZeroMemory(&rc, sizeof(rc));
        rc.right = i;
        rc.bottom = i * 2;
        FillRect(hc, &rc, (HBRUSH) (COLOR_WINDOW + 1));
        // replace black pen (can't be used with xor) with white
        DeleteObject(SelectObject(hc, CreatePen(PS_SOLID, 1, RGB(255, 255, 255))));
        // xor operation for lines of crosshair
        SetROP2(hc, R2_XORPEN);
      } else {
        DeleteDC(hc);
        hc = 0;
      }
    }
  }
  SetWndData(wnd, IDC_ZRGN, hc);
}

void ZoomAreaSize(HWND wnd, DWORD ctlidc) {
LONG i, k;
  // zoom factor
  k = 1 << (ctlidc - IDC_MUL1 + 1);
  i = GetWndData(wnd, IDC_ZOOM) / k;
  // must be odd for crosshair
  i -= (i & 1) ? 0 : 1;
  i *= k;
  // resize zoom window
  SetWindowPos(GetDlgItem(wnd, IDC_ZRGN), 0, 0, 0, i, i, SWP_NOMOVE);
}

// set window alpha if supported
typedef BOOL (WINAPI* LPSETLAYEREDWINDOWATTRIBUTES)(HWND hwnd, COLORREF crKey, BYTE bAlpha, DWORD dwFlags);
#ifndef LWA_ALPHA
#define LWA_ALPHA 2
#endif
BOOL SetWndAlpha(HWND wnd, BYTE bAlpha) {
LPSETLAYEREDWINDOWATTRIBUTES dl_SetLayeredWindowAttributes;
HMODULE hm;
BOOL result;
  result = FALSE;
  hm = LoadLibrary(TEXT("USER32.dll"));
  if (hm) {
    dl_SetLayeredWindowAttributes = (LPSETLAYEREDWINDOWATTRIBUTES) GetProcAddress(hm, "SetLayeredWindowAttributes");
    if (dl_SetLayeredWindowAttributes) {
      if (wnd) {
        if (bAlpha == 0xFF) {
          // https://learn.microsoft.com/en-us/windows/win32/winmsg/using-windows#using-layered-windows
          SetWindowLong(wnd, GWL_EXSTYLE, GetWindowLong(wnd, GWL_EXSTYLE) & ~WS_EX_LAYERED);
          RedrawWindow(wnd, NULL, NULL, RDW_ERASE | RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN);
        } else {
          SetWindowLong(wnd, GWL_EXSTYLE, GetWindowLong(wnd, GWL_EXSTYLE) | WS_EX_LAYERED);
          // https://msdn.microsoft.com/en-us/library/ms633540(VS.85).aspx
          result = dl_SetLayeredWindowAttributes(wnd, 0, bAlpha, LWA_ALPHA);
        }
      } else {
        // function available (Windows 2000 and newer)
        result = TRUE;
      }
    }
    FreeLibrary(hm);
  }
  return(result);
}

BOOL CtrlsUpdate(HWND wnd, DWORD atype, BOOL bmove) {
TCHAR c[16], *s;
POINT p, p_p;
HWND hw, p_h;
BOOL result;
HDC hd, hs;
RECT rc;
DWORD d;
  result = FALSE;
  do {
    // do not update anything if window minimized (faster)
    if (bmove && (IsDlgButtonChecked(wnd, IDC_WMIN) == BST_CHECKED)) { break; }
    // get cursor pos
    GetCursorPos(&p);
    // get window under cursor
    hw = WindowFromPoint(p);
    // stop if can't get window handle
    if (!hw) { break; }
    // save parent point and handle
    p_p = p;
    p_h = hw;
    // get parent window
    while (GetParent(p_h)) {
      p_h = GetParent(p_h);
    }
    // exclude this application window
    if (p_h == wnd) { break; }
    // http://www.powerbasic.com/support/pbforums/showthread.php?t=19931
    // filter out static controls (frames, images, etc.)
    if (GetParent(hw) && GetClassName(hw, c, 16) && (
      (lstrcmpi(c, WC_TABCONTROL) == 0) ||
      ((lstrcmpi(c, TEXT("BUTTON")) == 0) && ((GetWindowLong(hw, GWL_STYLE) & BS_GROUPBOX) == BS_GROUPBOX))
    )) {
      SetWindowPos(hw, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE);
      hw = GetParent(hw);
    }
    // child control
    ScreenToClient(hw, &p);
    hw = ChildWindowFromPoint(hw, p);
    // set result
    result = TRUE;
    // change window bits
    if (atype != IDC_VIEW) {
      // not moving
      if (!bmove) {
        // set window text
        switch (atype) {
          case IDC_STXT:
            // set window or item text
            s = GetWndText(GetDlgItem(wnd, IDC_TEXT));
            if (s) {
              SendMessage(hw ? hw : p_h, WM_SETTEXT, 0, (LPARAM) s);
              LocalFree(s);
            }
            break;
          case IDC_SENB:
          case IDC_SDIS:
            // only for child
            if (hw) {
              EnableWindow(hw, (atype == IDC_SENB) ? TRUE : FALSE);
            }
            break;
          case IDC_STOP:
          case IDC_SNRM:
            // only for parent
            SetWindowPos(p_h,
              (atype == IDC_STOP) ? HWND_TOPMOST : HWND_NOTOPMOST,
              0, 0, 0, 0, SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE
            );
            break;
          case IDC_SRSZ:
            // only for parent, Windows NT 5+ required
            SetWindowLong(p_h, GWL_STYLE, GetWindowLong(p_h, GWL_STYLE) | WS_SIZEBOX);
            break;
          case IDC_STRN:
            // only for parent, Windows NT 5+ required
            SetWndAlpha(p_h, SendDlgItemMessage(wnd, IDC_TRAN, TBM_GETPOS, 0, 0));
            break;
        }
      }
      // always break - do not update anything
      break;
    }
    // get mouse coordinates
    if (IsDlgButtonChecked(wnd, IDC_RPOS) == BST_CHECKED) {
      SetDlgItemInt(wnd, IDC_POSX, p_p.x, TRUE);
      SetDlgItemInt(wnd, IDC_POSY, p_p.y, TRUE);
    }
    // get pixel color
    if (IsDlgButtonChecked(wnd, IDC_RCLR) == BST_CHECKED) {
      hs = GetDC(0);
      if (hs) {
        hd = (HDC) GetWndData(wnd, IDC_ZRGN);
        if (hd) {
          // get pixel with transparent windows too (see comments below in zoom area for explanation)
          d = GetWndData(wnd, IDC_ZOOM);
          BitBlt(hd, 0, d, 1, 1,
            hs, p_p.x, p_p.y,
            SRCCOPY | (GetWndData(wnd, IDC_TRAN) ? CAPTUREBLT : 0)
          );
          d = GetPixel(hd, 0, d) & 0x00FFFFFF;
        } else {
          d = 0;
        }
        ReleaseDC(0, hs);
        // IDC_CLR1
        wsprintf(c, TEXT("#%02X%02X%02X"), GetRValue(d), GetGValue(d), GetBValue(d));
        SetDlgItemText(wnd, IDC_CLR1, c);
        // IDC_CLR2
        wsprintf(c, TEXT("%u %u %u"), GetRValue(d), GetGValue(d), GetBValue(d));
        SetDlgItemText(wnd, IDC_CLR2, c);
        // IDC_CLR3
        SetDlgItemInt(wnd, IDC_CLR3, d, FALSE);
        // IDC_CLR4
        SetWndData(wnd, IDC_CLR4, d);
        InvalidateRect(GetDlgItem(wnd, IDC_CLR4), NULL, FALSE);
      }
    }
    // get window text
    if (IsDlgButtonChecked(wnd, IDC_RTXT) == BST_CHECKED) {
      s = GetWndText(hw ? hw : p_h);
      if (s) {
        SetDlgItemText(wnd, IDC_TEXT, s);
        LocalFree(s);
      }
    }
    // get zoom area
    if (IsDlgButtonChecked(wnd, IDC_ZOOM) == BST_CHECKED) {
      hd = (HDC) GetWndData(wnd, IDC_ZRGN);
      if (hd) {
        ZeroMemory(&rc, sizeof(rc));
        rc.right = GetWndData(wnd, IDC_ZOOM);
        // clear all area
        rc.bottom = rc.right * 2;
        FillRect(hd, &rc, (HBRUSH) (COLOR_WINDOW + 1));
        rc.bottom /= 2;
        // zoom factor
        for (d = IDC_MUL1; d <= IDC_MUL3; d++) {
          if (IsDlgButtonChecked(wnd, d) == BST_CHECKED) { break; }
        }
        d = 1 << (d - IDC_MUL1 + 1);
        // draw zoomed image
        hs = GetDC(0);
        if (hs) {
          /*StretchBlt(hd, 0, 0, rc.right, rc.bottom,
            hs, p_p.x - (rc.right / (d * 2)), p_p.y - (rc.bottom / (d * 2)),
            rc.right / d, rc.bottom / d,
            SRCCOPY
          );*/
          // CAPTUREBLT required to capture semi-transparent windows but it works only with BitBlt()
          BitBlt(hd, 0, rc.bottom, rc.right, rc.bottom,
            hs, p_p.x - (rc.right / (d * 2)), p_p.y - (rc.bottom / (d * 2)),
            SRCCOPY | (GetWndData(wnd, IDC_TRAN) ? CAPTUREBLT : 0)
          );
          ReleaseDC(0, hs);
          // now captured image can be stretched
          StretchBlt(hd, 0, 0, rc.right, rc.bottom,
            hd, 0, rc.bottom,
            rc.right / d, rc.bottom / d,
            SRCCOPY
          );
        }
        // draw crosshair if required
        if (IsDlgButtonChecked(wnd, IDC_CRSS) == BST_CHECKED) {
          // vertical lines
          rc.left = (rc.right - d - 2) / 2;
          rc.top = rc.left + d + 1;
          MoveToEx(hd, rc.left, 0, NULL);
          LineTo(hd, rc.left, rc.bottom);
          MoveToEx(hd, rc.top, 0, NULL);
          LineTo(hd, rc.top, rc.bottom);
          // horizontal lines
          rc.left = (rc.bottom - d - 2) / 2;
          rc.top = rc.left + d + 1;
          MoveToEx(hd, 0, rc.left, NULL);
          LineTo(hd, rc.right, rc.left);
          MoveToEx(hd, 0, rc.top, NULL);
          LineTo(hd, rc.right, rc.top);
        }
        InvalidateRect(GetDlgItem(wnd, IDC_ZRGN), NULL, FALSE);
      }
    }
  } while (0);
  return(result);
}

// https://msdn.microsoft.com/en-us/library/windows/desktop/bb760250.aspx
// https://msdn.microsoft.com/en-us/library/windows/desktop/bb760252.aspx
// https://msdn.microsoft.com/en-us/library/windows/desktop/hh298368.aspx
void InitToolTip(HWND wnd, DWORD ntop, DWORD nend, DWORD nstr) {
TOOLINFO ti;
HWND hw;
DWORD i;
  if (IsWindow(wnd)) {
    // init structure
    ZeroMemory(&ti, sizeof(ti));
    ti.hinst = GetModuleHandle(NULL);
    // create tooltip
    hw = CreateWindowEx(
      0, TOOLTIPS_CLASS, NULL,
      WS_POPUP | TTS_ALWAYSTIP,
      CW_USEDEFAULT, CW_USEDEFAULT,
      CW_USEDEFAULT, CW_USEDEFAULT,
      wnd, NULL,
      ti.hinst, NULL
    );
    if (hw) {
      ti.cbSize = sizeof(ti);
      ti.hwnd = wnd;
      ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
      for (i = ntop; i <= nend; i++) {
        ti.uId = (UINT_PTR) GetDlgItem(wnd, i);
        ti.lpszText = MAKEINTRESOURCE(i - ntop + nstr);
        SendMessage(hw, TTM_ADDTOOL, 0, (LPARAM) &ti);
      }
      // activate
      SendMessage(wnd, TTM_ACTIVATE, (WPARAM) TRUE, 0);
      // save handle
      SetWndData(wnd, 0, hw);
    }
  }
}

void FreeToolTip(HWND wnd) {
HWND hw;
  if (IsWindow(wnd)) {
    // get handle
    hw = (HWND) GetWndData(wnd, 0);
    if (hw && IsWindow(hw)) {
      // remove from userdata
      SetWndData(wnd, 0, 0);
      // deactivate
      SendMessage(hw, TTM_ACTIVATE, (WPARAM) FALSE, 0);
      // destroy
      DestroyWindow(hw);
    }
  }
}

void URLOpenLink(HWND wnd, TCHAR *s) {
  CoInitialize(NULL);
  ShellExecute(wnd, NULL, s, NULL, NULL, SW_SHOWNORMAL);
  CoUninitialize();
}

// https://devblogs.microsoft.com/oldnewthing/20031107-00/?p=41923
// https://learn.microsoft.com/en-us/windows/win32/controls/common-control-window-classes
BOOL CALLBACK DlgPrc(HWND wnd, UINT msg, WPARAM wparm, LPARAM lparm) {
DRAWITEMSTRUCT *dis;
HIMAGELIST hil;
COLORREF cr[2];
BOOL result;
TCHAR *s, *d;
NMHDR *nmh;
HBRUSH hb;
RECT rc;
LONG i;
  result = FALSE;
  switch (msg) {
    case WM_INITDIALOG:
      // add icons
      SendMessage(wnd, WM_SETICON, ICON_BIG  , (LPARAM) LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_ICN)));
      SendMessage(wnd, WM_SETICON, ICON_SMALL, (LPARAM) LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_ICN)));
      // capture type
      SetWndData(wnd, IDC_VIEW, 0);
      // Windows 98: same color as zoom area
      SetWndData(wnd, IDC_CLR4, GetSysColor(COLOR_WINDOW) & 0xFFFFFF);
      // set alpha range
      SendDlgItemMessage(wnd, IDC_TRAN, TBM_SETRANGE, TRUE, MAKELONG(0, 255));
      // set alpha to max by default
      SendDlgItemMessage(wnd, IDC_TRAN, TBM_SETPOS, TRUE, 255);
      // all elements checked by default
      for (i = IDC_WMIN; i <= IDC_CRSS; i++) {
        CheckDlgButton(wnd, i, BST_CHECKED);
      }
      // alpha transparency not available on this Windows version
      if (!SetWndAlpha(NULL, 0)) {
        // disable control - not Windows 5.0 or newer
        EnableWindow(GetDlgItem(wnd, IDC_TRAN), FALSE);
        // flag for no alpha
        SetWndData(wnd, IDC_TRAN, 0);
      } else {
        // alpha present
        SetWndData(wnd, IDC_TRAN, 1);
      }
      // set bitmap images
      hil = ImageList_LoadBitmap(GetModuleHandle(NULL), MAKEINTRESOURCE(IDB_LST), 16, 0, RGB(0, 128, 128));
      if (hil) {
        for (i = IDC_VIEW; i <= IDC_STRN; i++) {
          SendDlgItemMessage(wnd, i, STM_SETICON, (WPARAM) ImageList_GetIcon(hil, i - IDC_VIEW, ILD_NORMAL), 0);
        }
        ImageList_Destroy(hil);
      }
      // create hints
      InitToolTip(wnd, IDC_VIEW, IDC_STRN, IDS_TTT_VIEW);
      // set zoom options
      CheckRadioButton(wnd, IDC_MUL1, IDC_MUL3, IDC_MUL1);
      // rectangle box must have equal sides for crosshair
      GetClientRect(GetDlgItem(wnd, IDC_ZRGN), &rc);
      // get min side - avoid out of window bounds
      i = (rc.right < rc.bottom) ? rc.right : rc.bottom;
      // must be even
      i -= (i & 1) ? 1 : 0;
      // set zoom max size
      SetWndData(wnd, IDC_ZOOM, i);
      // first option selected (resize zoom area)
      ZoomAreaSize(wnd, IDC_MUL1);
      // zoom area not initialized
      SetWndData(wnd, IDC_ZRGN, 0);
      // fall through
    case WM_DISPLAYCHANGE:
      // display settings change - reinit
      ZoomAreaInit(wnd, TRUE);
      // must be true
      result = TRUE;
      break;
    case WM_MOUSEMOVE:
      // drag and drop
      i = GetWndData(wnd, IDC_VIEW);
      if (i) {
        result = CtrlsUpdate(wnd, i, TRUE);
      }
      break;
    // FIXME: is this necessary?
    //case WM_KILLFOCUS:
    //case WM_CANCELMODE:
    //case WM_ENTERIDLE:
    case WM_LBUTTONUP:
      // drag and drop
      if (GetWndData(wnd, IDC_VIEW)) {
        ReleaseCapture();
      }
      break;
    case WM_CAPTURECHANGED:
      // drag and drop
      i = GetWndData(wnd, IDC_VIEW);
      if (i) {
        // restore cursor
        SetCursor(LoadCursor(NULL, IDC_ARROW));
        // release capture
        ReleaseCapture();
        // update info
        CtrlsUpdate(wnd, i, FALSE);
        // remove drag and drop
        SetWndData(wnd, IDC_VIEW, 0);
        // restore window if it was minimized
        if (IsDlgButtonChecked(wnd, IDC_WMIN) == BST_CHECKED) {
          ShowWindow(wnd, SW_RESTORE);
        }
        result = TRUE;
      }
      break;
    case WM_NOTIFY:
      nmh = (NMHDR *) lparm;
      if (nmh && (nmh->idFrom == IDC_TRAN)) {
        // display alpha value number
        SetDlgItemInt(wnd, IDC_AVAL, SendDlgItemMessage(wnd, IDC_TRAN, TBM_GETPOS, 0, 0), TRUE);
        result = TRUE;
      }
      break;
    case WM_COMMAND:
      if (HIWORD(wparm) == BN_CLICKED) {
        switch (LOWORD(wparm)) {
          case IDC_VIEW:
          case IDC_STXT:
          case IDC_SENB:
          case IDC_SDIS:
          case IDC_STOP:
          case IDC_SNRM:
          case IDC_SRSZ:
          case IDC_STRN:
            // set drag and drop mode
            SetWndData(wnd, IDC_VIEW, LOWORD(wparm));
            // minimize windows if required
            if (IsDlgButtonChecked(wnd, IDC_WMIN) == BST_CHECKED) {
              ShowWindow(wnd, SW_MINIMIZE);
            }
            // start capture
            SetCapture(wnd);
            // change cursor
            SetCursor(LoadCursor(NULL, IDC_CROSS));
            break;
          case IDC_MUL1:
          case IDC_MUL2:
          case IDC_MUL3:
            ZoomAreaSize(wnd, LOWORD(wparm));
            result = TRUE;
            break;
          case IDCANCEL:
            // free zoom image
            ZoomAreaInit(wnd, FALSE);
            // free tool tips
            FreeToolTip(wnd);
            EndDialog(wnd, 0);
            result = TRUE;
            break;
          case IDC_SITE:
            // get control text
            s = GetWndText(GetDlgItem(wnd, LOWORD(wparm)));
            if (s) {
              // save original pointer
              d = s;
              // find link splitter
              for (; *s; s++) {
                // found it
                if (*s == TEXT('|')) {
                  break;
                }
              }
              // found?
              if (*s == TEXT('|')) {
                // remove space if any
                for (s++; *s == TEXT(' '); s++);
              }
              // open link
              if (*s) {
                URLOpenLink(wnd, s);
              }
              // free memory
              LocalFree(d);
            }
            break;
        }
      }
      break;
    case WM_SETCURSOR:
      // site link
      if (((HWND) wparm) == GetDlgItem(wnd, IDC_SITE)) {
        SetCursor(LoadCursor(NULL, IDC_HAND));
        SetWindowLongPtr(wnd, DWLP_MSGRESULT, TRUE);
        result = TRUE;
        break;
      }
      // drag and drop controls
      for (i = IDC_VIEW; i <= IDC_STRN; i++) {
        if (((HWND) wparm) == GetDlgItem(wnd, i)) {
          SetCursor(LoadCursor(NULL, IDC_HAND));
          SetWindowLongPtr(wnd, DWLP_MSGRESULT, TRUE);
          result = TRUE;
          break;
        }
      }
      break;
    case WM_DRAWITEM:
      dis = (DRAWITEMSTRUCT *) lparm;
      if (!dis) { break; }
      // [!] MSDN on WM_DRAWITEM: Before returning from processing this message,
      // an application should ensure that the device context identified by
      // the hDC member of the DRAWITEMSTRUCT structure is in the default state.
      switch (dis->CtlID) {
        case IDC_SITE:
          s = GetWndText(dis->hwndItem);
          if (s) {
            // change hDC
            cr[0] = SetTextColor(dis->hDC, RGB(0, 0, 0xFF));
            cr[1] = SetBkMode(dis->hDC, TRANSPARENT);
            // draw
            DrawText(dis->hDC, s, -1, &dis->rcItem, DT_LEFT | DT_TOP | DT_SINGLELINE);
            // [!] restore hDC
            SetBkMode(dis->hDC, cr[1]);
            SetTextColor(dis->hDC, cr[0]);
            LocalFree(s);
            result = TRUE;
          }
          break;
        case IDC_CLR4:
          // https://devblogs.microsoft.com/oldnewthing/20100409-00/?p=14363
          i = GetWndData(wnd, IDC_CLR4);
          GetClientRect(dis->hwndItem, &rc);
          // create solid brush
          hb = CreateSolidBrush(i);
          if (hb) {
            // set new brush
            hb = SelectObject(dis->hDC, hb);
            // draw rectangle with borders
            Rectangle(dis->hDC, rc.left, rc.top, rc.right, rc.bottom);
            // [!] restore brush to prevent GDI handle leaks
            hb = SelectObject(dis->hDC, hb);
            // delete brush
            DeleteObject(hb);
          }
          result = TRUE;
          break;
        case IDC_ZRGN:
          GetClientRect(dis->hwndItem, &rc);
          BitBlt(dis->hDC, 0, 0, rc.right, rc.bottom, (HDC) GetWndData(wnd, dis->CtlID), 0, 0, SRCCOPY);
          result = TRUE;
          break;
      }
      break;
  }
  return(result);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
  InitCommonControls();
  DialogBoxParam(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_DLG), 0, &DlgPrc, 0);
  ExitProcess(0);
  return(0);
}
