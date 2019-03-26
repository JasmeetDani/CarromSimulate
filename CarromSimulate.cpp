//--------------------------------------------------------------------------------------------

#include "stdafx.h"
#include <windows.h>
#include <vector>
#include <ctime>

#include <math.h>

#include "resource.h"

#define COEFF_RES 1
#define MASS 1
#define MAG 40.0

#define KEYDOWN(vk_code) ((GetAsyncKeyState(vk_code) & 0x8000) ? 1 : 0)
#define CAPS_ON(vk_code) ((GetKeyState(vk_code) & 0x0001) ? 1 : 0)

//--------------------------------------------------------------------------------------------

LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);


HINSTANCE hInstance;


struct INFO
{
	double x, y;

	double vx, vy;

	int index;

} *pBall;


namespace
{
	int xa, ya, xb, yb;
	int pxa, pya, pxb, pyb;

	std::vector<INFO*> Objects;

	std::vector<POINT> Dirty;

	BOOL bTracking, bPause = FALSE;

	int Width, Height;

	int Game_ObjectBitmaps[3] = { IDB_BITMAP1, IDB_BITMAP2, IDB_BITMAP3 };

	bool bLineDirty = false, bDrag = false;
}


int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR pszCmdLine, int iCmdShow)
{
	static TCHAR szAppName[] = TEXT("Game Loop Demo");
	HWND MainWindow;

	WNDCLASS wndClass;

	MSG msg;

	HBITMAP bmp;

	UINT XDIMENSION = GetSystemMetrics(SM_CXSCREEN);
	UINT YDIMENSION = GetSystemMetrics(SM_CYSCREEN);

	wndClass.lpszClassName = szAppName;
	wndClass.hInstance = hInstance;
	wndClass.lpfnWndProc = WndProc;
	wndClass.cbClsExtra = 0;
	wndClass.cbWndExtra = 0;
	wndClass.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
	wndClass.hCursor = LoadCursor(NULL, IDC_CROSS);
	wndClass.hIcon = LoadIcon(NULL, IDI_APPLICATION);
	wndClass.lpszMenuName = NULL;
	wndClass.style = CS_HREDRAW | CS_VREDRAW;

	if (!RegisterClass(&wndClass))
		return 0;

	::hInstance = hInstance;

	MainWindow = CreateWindow(szAppName, szAppName, WS_POPUP | WS_VISIBLE,
		CW_USEDEFAULT, CW_USEDEFAULT,
		XDIMENSION, YDIMENSION,
		NULL, NULL, hInstance, NULL);

	if (!MainWindow)
		return 0;

	UpdateWindow(MainWindow);

	//----------------------------------------------------------------------------------------

	// perform the initialisation chores

	HDC hdc = GetDC(MainWindow);

	bmp = CreateCompatibleBitmap(hdc, XDIMENSION, YDIMENSION);

	HDC hMemDC = CreateCompatibleDC(hdc);

	SelectObject(hMemDC, bmp);

	HPEN OldPen = (HPEN)SelectObject(hMemDC, GetStockObject(NULL_PEN));
	HBRUSH OldBrush = (HBRUSH)SelectObject(hMemDC, GetStockObject(BLACK_BRUSH));

	// int Diff = 0 ; // (XDIMENSION - YDIMENSION) / 2;

	// Rectangle (hMemDC, 0, 0, XDIMENSION, YDIMENSION) ;

	HBITMAP Object[3];

	int i;

	for (i = 0; i < 3; ++i)
	{
		Object[i] = LoadBitmap(hInstance, MAKEINTRESOURCE(Game_ObjectBitmaps[i]));
	}

	BITMAP bm;
	GetObject(Object[0], sizeof(BITMAP), &bm);

	Width = bm.bmWidth + 1;
	Height = bm.bmHeight + 1;

	int Radius = Width;
	double RadiusSq = Radius * Radius;

	HDC hObjectDC[3];

	// create the offscreen dc's for the game bitmaps

	for (i = 0; i < 3; ++i)
	{
		hObjectDC[i] = CreateCompatibleDC(hdc);

		SelectObject(hObjectDC[i], Object[i]);
	}

	RECT rcClient;

	GetClientRect(MainWindow, &rcClient);

	// rcClient.left += Diff, rcClient.right -= Diff;

	//----------------------------------------------------------------------------------------

	int j, xOverLap, yOverLap;
	double dx, dy, d2x, d2y;

	POINT PT;

	// enter the game loop

	while (TRUE)
	{
		if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
		{
			// test if this is a quit

			if (msg.message == WM_QUIT)
				break;

			// translate any accelerator keys

			TranslateMessage(&msg);

			// send the message to the window proc

			DispatchMessage(&msg);
		}

		// main game processing goes here

		if (KEYDOWN(VK_ESCAPE))
			SendMessage(MainWindow, WM_CLOSE, 0, 0);

		if (!bPause)
		{
			// redraw the dirty rectangles

			for (i = 0; i < Dirty.size(); ++i)
			{
				PT.x = Dirty[i].x;
				PT.y = Dirty[i].y;

				Rectangle(hMemDC, PT.x, PT.y, PT.x + Width, PT.y + Height);
			}

			Dirty.clear();

			// erase the shooter band if dirty

			if (bLineDirty)
			{
				HPEN OldPen = (HPEN)SelectObject(hMemDC, GetStockObject(BLACK_PEN));

				if (bTracking)
				{
					MoveToEx(hMemDC, pxa, pya, NULL);
					LineTo(hMemDC, pxb, pyb);
				}

				else
				{
					MoveToEx(hMemDC, xa, ya, NULL);
					LineTo(hMemDC, xb, yb);

					bLineDirty = false;
				}


				SelectObject(hMemDC, OldPen);
			}

			// this loop does all the real physics to determine if there has
			// been a collision between any ball and any other ball; if there is a
			// collision, the function uses the mass of each ball along with the
			// initial velocities to compute the resulting velocities

			// we know that in general
			// va2 = (e + 1) * mb * vb1 + va1(ma - e * mb) / (ma + mb)
			// vb2 = (e + 1) * ma * va1 + vb1(ma - e * mb) / (ma + mb)

			// and the objects will have direction vectors co-linear to the normal
			// of the point of collision, but since we are using spheres here as the
			// objects, we know that the normal to the point of collision is just
			// the vector from the centers of each object, thus the resulting
			// velocity vector of each ball will be along this normal vector direction

			// test each object against each other object and test for a
			// collision; there are better ways to do this other than a double nested
			// loop, but since there are a small number of objects this is fine;

			for (i = 0; i < Objects.size(); ++i)
			{
				for (j = i + 1; j < Objects.size(); ++j)
				{
					dx = Objects[i]->x - Objects[j]->x;
					dy = Objects[i]->y - Objects[j]->y;

					d2x = dx * dx;
					d2y = dy * dy;

					// check for bounding circle intersections

					if ((d2x + d2y) < RadiusSq)
					{
						// the balls have made contact, compute response

						double dist = sqrt(d2x + d2y);

						// compute the response coordinate system axes
						// normalize normal vector

						double nabx = dx / dist;
						double naby = dy / dist;

						// compute the tangential vector perpendicular to normal,
						// simply rotate vector 90

						double tabx = -naby;
						double taby = nabx;

						// tangential is also normalized since
						// it’s just a rotated normal vector

						// now compute all the initial velocities
						// notation ball: (a,b) initial: i, final: f,
						// n: normal direction, t: tangential direction

						double vait = Objects[i]->vx * tabx + Objects[i]->vy * taby;
						double vain = Objects[i]->vx * nabx + Objects[i]->vy * naby;

						double vbit = Objects[j]->vx * tabx + Objects[j]->vy * taby;
						double vbin = Objects[j]->vx * nabx + Objects[j]->vy * naby;


						double vafn = (MASS * vbin * (COEFF_RES + 1) + vain *
							(MASS - COEFF_RES * MASS)) / (MASS + MASS);

						double vbfn = (MASS * vain * (COEFF_RES + 1) - vbin *
							(MASS - COEFF_RES * MASS)) / (MASS + MASS);

						// now luckily the tangential components
						// are the same before and after, so

						double vaft = vait;
						double vbft = vbit;

						// and that’s that baby!
						// the velocity vectors are:
						// object a (vafn, vaft)
						// object b (vbfn, vbft)

						// the only problem is that we are in the wrong coordinate
						// system! we need to translate back to the original x,y
						// coordinate system; basically we need to
						// compute the sum of the x components relative to
						// the n,t axes and the sum of
						// the y components relative to the n,t axis,
						// since n,t may both have x,y
						// components in the original x,y coordinate system

						double xfa = vafn * nabx + vaft * tabx;
						double yfa = vafn * naby + vaft * taby;

						double xfb = vbfn * nabx + vbft * tabx;
						double yfb = vbfn * naby + vbft * taby;

						// set the final velocities

						Objects[i]->vx = xfa;
						Objects[i]->vy = yfa;

						Objects[j]->vx = xfb;
						Objects[j]->vy = yfb;

						// bPause = TRUE ;

						// break ;
					}
				}

			}

			// move and render the balls 

			for (i = 0; i < Objects.size(); ++i)
			{
				Objects[i]->vx /= 1.005;
				Objects[i]->vy /= 1.005;

				PT.x = Objects[i]->x + Objects[i]->vx;
				PT.y = Objects[i]->y + Objects[i]->vy;

				if ((xOverLap = (PT.x + Width - rcClient.right)) > 0)
				{
					Objects[i]->x = (PT.x -= xOverLap);
					Objects[i]->vx *= -1;
				}

				else
				{
					if ((xOverLap = (rcClient.left - PT.x)) > 0)
					{
						Objects[i]->x = (PT.x += xOverLap);
						Objects[i]->vx *= -1;
					}

					else
					{
						Objects[i]->x += Objects[i]->vx;

						PT.x = Objects[i]->x;
					}
				}

				if ((yOverLap = (PT.y + Height - rcClient.bottom)) > 0)
				{
					Objects[i]->y = (PT.y -= yOverLap);
					Objects[i]->vy *= -1;
				}

				else
				{
					if ((yOverLap = (rcClient.top - PT.y)) > 0)
					{
						Objects[i]->y = (PT.y += yOverLap);
						Objects[i]->vy *= -1;
					}

					else
					{
						Objects[i]->y += Objects[i]->vy;

						PT.y = Objects[i]->y;
					}
				}

				BitBlt(hMemDC, PT.x, PT.y, Width, Height, hObjectDC[Objects[i]->index],
					0, 0, SRCCOPY);

				Dirty.push_back(PT);
			}

			if (bLineDirty)
			{
				HPEN OldPen = (HPEN)SelectObject(hMemDC, GetStockObject(WHITE_PEN));

				MoveToEx(hMemDC, xa, ya, NULL);
				LineTo(hMemDC, xb, yb);

				SelectObject(hMemDC, OldPen);
			}
		}

		// draw the scene

		BitBlt(hdc, 0, 0, XDIMENSION, YDIMENSION, hMemDC, 0, 0, SRCCOPY);

		Sleep(1);
	}

	// end of game loop

	//----------------------------------------------------------------------------------------

	// perform the cleanup

	SelectObject(hMemDC, OldPen), SelectObject(hMemDC, OldBrush);

	DeleteDC(hMemDC), DeleteObject(bmp);

	for (i = 0; i < 3; ++i)
	{
		DeleteDC(hObjectDC[i]), DeleteObject(Object[i]);
	}

	ReleaseDC(MainWindow, hdc);

	//----------------------------------------------------------------------------------------

	return(msg.wParam);
}


LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	int i;

	int xStart, yStart, xEnd, yEnd, dx, dy;

	static INFO* ptr = NULL;

	HDC hdc;

	PAINTSTRUCT ps;

	switch (message)
	{
	case WM_KEYDOWN:

		if (wParam == VK_DOWN) bPause = !bPause;

		break;

	case WM_LBUTTONDOWN:

		/*pBall = new INFO ;

		pBall->x = LOWORD (lParam) ;
		pBall->y = HIWORD (lParam) ;

		pBall->vx = 0 ;
		pBall->vy = -10 ;

		Objects.push_back (pBall) ;*/

		bTracking = TRUE;

		xa = LOWORD(lParam);
		ya = HIWORD(lParam);

		for (i = 0; i < Objects.size(); ++i)
		{
			xStart = (int)Objects[i]->x;
			xEnd = xStart + Width;

			if (xa >= xStart && xa <= xEnd)
			{
				yStart = (int)Objects[i]->y;
				yEnd = yStart + Height;

				if (ya >= yStart && ya <= yEnd)
				{
					ptr = Objects[i];

					ptr->vx = ptr->vy = 0;

					if (CAPS_ON(VK_CAPITAL))
					{
						bDrag = true;
					}

					else
					{
						xa = xStart + (xEnd - xStart) / 2;
						ya = yStart + (yEnd - yStart) / 2;
					}

					break;
				}
			}
		}

		SetCapture(hwnd);

		break;

	case WM_RBUTTONDOWN:

		pBall = new INFO;

		pBall->x = LOWORD(lParam) - Width / 2;
		pBall->y = HIWORD(lParam) - Height / 2;

		pBall->vx = 0;
		pBall->vy = 0;
		pBall->index = rand() % 3;

		Objects.push_back(pBall);

	case WM_LBUTTONUP:

		if (bTracking)
		{
			/*

			bTracking = FALSE ;

			pBall = new INFO ;

			pBall->x = LOWORD (lParam) ;
			pBall->y = HIWORD (lParam) ;

			pBall->vx = (xa - xb) / 50 ;
			pBall->vy = (ya - yb) / 50 ;
			pBall->index = rand() % 3 ;

			Objects.push_back (pBall) ;

			*/

			bTracking = FALSE;

			if (bDrag) bDrag = false;

			else
			{
				if (ptr != NULL)
				{
					dx = xb - xa;
					dy = yb - ya;

					ptr->vx = dx / MAG;
					ptr->vy = dy / MAG;

					ptr = NULL;
				}
			}

			ReleaseCapture();
		}

		break;

	case WM_MOUSEMOVE:

		if (ptr != NULL)
		{
			if (bDrag)
			{
				xb = LOWORD(lParam);
				yb = HIWORD(lParam);

				ptr->x = xb - Width / 2;
				ptr->y = yb - Height / 2;

				break;
			}

			else
			{
				pxa = xa, pya = ya, pxb = xb, pyb = yb;

				bLineDirty = true;
			}

			xb = LOWORD(lParam);
			yb = HIWORD(lParam);
		}

		break;

	case WM_PAINT:

		hdc = BeginPaint(hwnd, &ps);

		EndPaint(hwnd, &ps);

		return 0;

	case WM_DESTROY:

		for (int i = 0; i < Objects.size(); ++i)
		{
			delete Objects[i];
		}

		Objects.clear();

		PostQuitMessage(0);

		return 0;
	}

	return DefWindowProc(hwnd, message, wParam, lParam);
}