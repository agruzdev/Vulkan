// Copyright 2016 Intel Corporation All Rights Reserved
// 
// Intel makes no representations about the suitability of this software for any purpose.
// THIS SOFTWARE IS PROVIDED ""AS IS."" INTEL SPECIFICALLY DISCLAIMS ALL WARRANTIES,
// EXPRESS OR IMPLIED, AND ALL LIABILITY, INCLUDING CONSEQUENTIAL AND OTHER INDIRECT DAMAGES,
// FOR THE USE OF THIS SOFTWARE, INCLUDING LIABILITY FOR INFRINGEMENT OF ANY PROPRIETARY
// RIGHTS, AND INCLUDING THE WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
// Intel does not assume any responsibility for any errors which may appear in this software
// nor any responsibility to update it.

#if !defined(OPERATING_SYSTEM_HEADER)
#define OPERATING_SYSTEM_HEADER

#if defined(VK_USE_PLATFORM_WIN32_KHR)
#include <Windows.h>
#include <Windowsx.h>

#elif defined(VK_USE_PLATFORM_XCB_KHR)
#include <xcb/xcb.h>
#include <dlfcn.h>
#include <cstdlib>

#elif defined(VK_USE_PLATFORM_XLIB_KHR)
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <dlfcn.h>
#include <cstdlib>

#endif

#include <cstring>
#include <iostream>

namespace ApiWithoutSecrets {

  namespace OS {

    // ************************************************************ //
    // LibraryHandle                                                //
    //                                                              //
    // Dynamic Library OS dependent type                            //
    // ************************************************************ //
    // 
#if defined(VK_USE_PLATFORM_WIN32_KHR)
    typedef HMODULE LibraryHandle;

#elif defined(VK_USE_PLATFORM_XCB_KHR) || defined(VK_USE_PLATFORM_XLIB_KHR)
    typedef void* LibraryHandle;

#endif

    // ************************************************************ //
    // OnWindowSizeChanged                                          //
    //                                                              //
    // Base class for handling window size changes                  //
    // ************************************************************ //
    class TutorialBase {
    public:
      virtual bool OnWindowSizeChanged() = 0;
      virtual bool Draw() = 0;
      virtual void Shutdown() {};

      virtual bool ReadyToDraw() const final {
        return CanRender;
      }

      TutorialBase() :
        CanRender( false ) {
      }

      virtual ~TutorialBase() {
      }

    protected:
      bool CanRender;
    };

    // ************************************************************ //
    // WindowParameters                                             //
    //                                                              //
    // OS dependent window parameters                               //
    // ************************************************************ //
    struct WindowParameters {
#if defined(VK_USE_PLATFORM_WIN32_KHR)
      HINSTANCE           Instance;
      HWND                Handle;

      WindowParameters() :
        Instance(),
        Handle() {
      }

#elif defined(VK_USE_PLATFORM_XCB_KHR)
      xcb_connection_t   *Connection;
      xcb_window_t        Handle;

      WindowParameters() :
        Connection(),
        Handle() {
      }

#elif defined(VK_USE_PLATFORM_XLIB_KHR)
      Display            *DisplayPtr;
      Window              Handle;

      WindowParameters() :
        DisplayPtr(),
        Handle() {
      }

#endif
    };


    enum class MouseEvent
    {
        Down,
        Move, 
        Up
    };

    // ************************************************************ //
    // Window                                                       //
    //                                                              //
    // Interface for processing mouse events                        //
    // ************************************************************ //
    class MouseListener {
    public:
        virtual ~MouseListener() = default;

        virtual void OnMouseEvent(MouseEvent event, int x, int y) = 0;
    };


    // ************************************************************ //
    // Window                                                       //
    //                                                              //
    // OS dependent window creation and destruction class           //
    // ************************************************************ //
    class Window {
    public:
      Window();
      ~Window();

      bool              Create( const char *title, uint32_t width, uint32_t height );
      bool              RenderingLoop( TutorialBase &tutorial ) const;
      WindowParameters  GetParameters() const;

      void SetMouseListener(MouseListener * listener);

    private:
      WindowParameters  Parameters;
    };

  } // namespace OS

} // namespace ApiWithoutSecrets

#endif // OPERATING_SYSTEM_HEADER