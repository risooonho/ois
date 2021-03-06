/*
The zlib/libpng License

Copyright (c) 2005-2007 Phillip Castaneda (pjcast -- www.wreckedgames.com)

This software is provided 'as-is', without any express or implied warranty. In no event will
the authors be held liable for any damages arising from the use of this software.

Permission is granted to anyone to use this software for any purpose, including commercial 
applications, and to alter it and redistribute it freely, subject to the following
restrictions:

    1. The origin of this software must not be misrepresented; you must not claim that 
		you wrote the original software. If you use this software in a product, 
		an acknowledgment in the product documentation would be appreciated but is 
		not required.

    2. Altered source versions must be plainly marked as such, and must not be 
		misrepresented as being the original software.

    3. This notice may not be removed or altered friosom any source distribution.
*/
#include "linux/LinuxMouse.h"
#include "linux/LinuxInputManager.h"
#include "OISException.h"
#include "OISEvents.h"

using namespace OIS;

//-------------------------------------------------------------------//
LinuxMouse::LinuxMouse(InputManager* creator, bool buffered, bool grab, bool hide)
	: Mouse(creator->inputSystemName(), buffered, 0, creator)
{
	display = 0;
	window = 0;
	cursor = 0;

	grabMouse = grab;
	hideMouse = hide;

	_grabMouse = false;
	_hideMouse = false;

	static_cast<LinuxInputManager*>(mCreator)->_setMouseUsed(true);
}

//-------------------------------------------------------------------//
void LinuxMouse::_initialize()
{
	//Clear old state
	mState.clear();
	mMoved  = false;
	mWarped = false;

	oldXMouseX = oldXMouseY = oldXMouseZ = 0;

	if( display ) XCloseDisplay(display);
	display = 0;
	window = static_cast<LinuxInputManager*>(mCreator)->_getWindow();

	//Create our local X mListener connection
	if( !(display = XOpenDisplay(0)) )
		OIS_EXCEPT(E_General, "LinuxMouse::_initialize >> Error opening X!");

	//Set it to recieve Mouse Input events
	if( XSelectInput(display, window, ButtonPressMask | ButtonReleaseMask | PointerMotionMask | EnterWindowMask | LeaveWindowMask) == BadWindow )
		OIS_EXCEPT(E_General, "LinuxMouse::_initialize >> X error!");

	//Create a blank cursor:
	Pixmap bm_no;
	XColor black, dummy;
	Colormap colormap;
	static char no_data[] = { 0,0,0,0,0,0,0,0 };

	colormap = DefaultColormap( display, DefaultScreen(display) );
	XAllocNamedColor( display, colormap, "black", &black, &dummy );
	bm_no = XCreateBitmapFromData( display, window, no_data, 8, 8 );
	cursor = XCreatePixmapCursor( display, bm_no, bm_no, &black, &black, 0, 0 );

	Window focusWindow;
	int focusWindowState;
	Display* disp = XOpenDisplay(0);
	XGetInputFocus(disp, &focusWindow, &focusWindowState);
	XCloseDisplay(disp);
	if(focusWindow != window)
	{
		//don't grab input, if we don't have focus.
		static_cast<LinuxInputManager*>(mCreator)->_setWindowFocus(false);
	}

	//set internal grab state to the inverse of user requested to force a hide and grab.
	_grabMouse = !grabMouse;
	_hideMouse = !hideMouse;

	_hide( hideMouse );
	_grab( grabMouse );

	mouseFocusLost = false;
}

//-------------------------------------------------------------------//
LinuxMouse::~LinuxMouse()
{
	if( display )
	{
		_grab(false);
		_hide(false);
		XFreeCursor(display, cursor);
		XCloseDisplay(display);
	}

	static_cast<LinuxInputManager*>(mCreator)->_setMouseUsed(false);
}

//-------------------------------------------------------------------//
void LinuxMouse::setBuffered(bool buffered)
{
	mBuffered = buffered;
}

//-------------------------------------------------------------------//
void LinuxMouse::capture()
{
	//Clear out last frames values
	mState.X.rel = 0;
	mState.Y.rel = 0;
	mState.Z.rel = 0;

	_processXEvents();

	mWarped = false;

	if( mMoved == true )
	{
		if( mBuffered && mListener )
			mListener->mouseMoved( MouseEvent( this, mState ) );

		mMoved = false;
	}


	//Check for losing/gaining mouse grab focus (alt-tab, etc)
	LinuxInputManager* inputManager = static_cast<LinuxInputManager*>(mCreator);
	bool hasFocus = inputManager->_hasWindowFocus();

	if( _grabMouse && !hasFocus )
	{
		if( !mouseFocusLost )	//We just loose mouse grab focus
		{
			//Don't change the order of the following 3 lines!
			_grab( false );
			_hide( false );
			mouseFocusLost = true;
		}
	}
	else if( mouseFocusLost && hasFocus )	//We just regained mouse grab focus
	{
		//Don't change the order of the following 3 lines!
		mouseFocusLost = false;
		_hide( hideMouse );
		_grab( grabMouse );
	}
}

//-------------------------------------------------------------------//
void LinuxMouse::_processXEvents()
{
	//X11 Button Events: 1=left 2=middle 3=right; Our Bit Postion: 1=Left 2=Right 3=Middle
	char mask[4] = {0,1,4,2};
	XEvent event;

	//Poll x11 for events mouse events
	while( XPending(display) > 0 ) 
	{
		XNextEvent(display, &event);

		switch(event.type)
		{
		case LeaveNotify:
			//Mouse moved
			_injectMouseMoved(event.xmotion.x, event.xmotion.y);
			break;
		case MotionNotify:
		case EnterNotify:
			//Mouse moved
			_injectMouseMoved(event.xcrossing.x, event.xcrossing.y);
			break;
		case ButtonPress:
			//Button down
			static_cast<LinuxInputManager*>(mCreator)->_setWindowFocus(true);

			if( event.xbutton.button < 4 )
			{
				mState.buttons |= mask[event.xbutton.button];
				if( mBuffered && mListener )
					if( mListener->mousePressed( MouseEvent( this, mState ),
						(MouseButtonID)(mask[event.xbutton.button] >> 1)) == false )
						return;
			}
			break;
		case ButtonRelease:
			//Button up
			if( event.xbutton.button < 4 )
			{
				mState.buttons &= ~mask[event.xbutton.button];
				if( mBuffered && mListener )
					if( mListener->mouseReleased( MouseEvent( this, mState ),
						(MouseButtonID)(mask[event.xbutton.button] >> 1)) == false )
						return;
			}
			//The Z axis gets pushed/released pair message (this is up)
			else if( event.xbutton.button == 4 )
			{
				mState.Z.rel += 120;
				mState.Z.abs += 120;
				mMoved = true;
			}
			//The Z axis gets pushed/released pair message (this is down)
			else if( event.xbutton.button == 5 )
			{
				mState.Z.rel -= 120;
				mState.Z.abs -= 120;
				mMoved = true;
			}
			break;
		}
	}
}

void LinuxMouse::_injectMouseMoved(int x, int y)
{
	if(x == oldXMouseX && y == oldXMouseY)
	{
		return;
	}

	//Ignore out of bounds mouse if we just warped
	if( mWarped )
	{
		if(x < 5 || x > mState.width - 5 ||
		   y < 5 || y > mState.height - 5)
			return;
	}

	//Compute this frames Relative X & Y motion
	int dx = x - oldXMouseX;
	int dy = y - oldXMouseY;

	//Store old values for next time to compute relative motion
	oldXMouseX = x;
	oldXMouseY = y;

	if( grabMouse && !mouseFocusLost)
	{
		mState.X.abs += dx;
		mState.Y.abs += dy;
	}
	else
	{
		mState.X.abs = x;
		mState.Y.abs = y;
	}
	mState.X.rel += dx;
	mState.Y.rel += dy;

	//Check to see if we are grabbing the mouse to the window (requires clipping and warping)
	if( mState.X.abs < 0 )
		mState.X.abs = 0;
	else if( mState.X.abs > mState.width )
		mState.X.abs = mState.width;

	if( mState.Y.abs < 0 )
		mState.Y.abs = 0;
	else if( mState.Y.abs > mState.height )
		mState.Y.abs = mState.height;

	if( grabMouse && !mWarped && !mouseFocusLost)
	{
		//Keep mouse in window (fudge factor)
		if(x < 5 || x > mState.width - 5 ||
		   y < 5 || y > mState.height - 5 )
		{
			oldXMouseX = mState.width >> 1; //center x
			oldXMouseY = mState.height >> 1; //center y
			XWarpPointer(display, None, window, 0, 0, 0, 0, oldXMouseX, oldXMouseY);
			mWarped = true;
		}
	}

	mMoved = true;
}

//-------------------------------------------------------------------//
void LinuxMouse::_grab(bool grab)
{

	//Never grab, when lost window focus.
	if(mouseFocusLost || _grabMouse == grab)
	{
		return;
	}

	_grabMouse = grab;

	// We need to set keyboard grab too or the keyboard will not receive alt+tab event
	// and the user can't leave mouse grab manually.
	static_cast<LinuxInputManager*>(mCreator)->_setKeyboardGrabState(grab);

	if( grab )
	{
		grabX = mState.X.abs;
		grabY = mState.Y.abs;
		XGrabPointer(display, window, True, 0, GrabModeAsync, GrabModeAsync, window, None, CurrentTime);
		_hide(true);
	}
	else
	{
		XUngrabPointer(display, CurrentTime);
		setPosition(grabX, grabY);
		_hide(hideMouse);
	}
}

//-------------------------------------------------------------------//
void LinuxMouse::grab(bool grab)
{
	grabMouse = grab;
	_grab(grab);
}

void LinuxMouse::_hide(bool hide)
{
	//Never hide, when lost focus!
	if(mouseFocusLost || _hideMouse == hide)
	{
		return;
	}

	_hideMouse = hide;

	if( hide )
		//Define the invisible cursor.
		XDefineCursor(display, window, cursor);
	else
		//Undefine the invisible cursor.
		XUndefineCursor(display, window);
}

//-------------------------------------------------------------------//
void LinuxMouse::hide(bool hide)
{
	hideMouse = hide;
	_hide( hide );
}

//-------------------------------------------------------------------//
void LinuxMouse::setPosition(unsigned int x, unsigned int y)
{
	if( mouseFocusLost )
	{
		return;
	}
	if(_grabMouse)
	{
		grabX = mState.X.abs;
		grabY = mState.Y.abs;
		mState.X.abs = x;
		mState.Y.abs = y;
	}
	else
	{
		XWarpPointer(display, None, window, 0, 0, 0, 0, x, y);
		oldXMouseX = x;
		oldXMouseY = y;
		mWarped = true;
	}
}
