#include "Stdafx.h"
#include "CefSharpRenderDelegate.h"


namespace CefSharp
{
    namespace Internals
    {
        bool CefSharpRenderDelegate::GetScreenInfo(CefScreenInfo& screen_info)
        {
            return false;

            auto screenInfo = _renderWebBrowser->GetScreenInfo();

            if (screen_info.device_scale_factor == screenInfo.ScaleFactor)
            {
                return false;
            }

            screen_info.device_scale_factor = screenInfo.ScaleFactor;
            return true;
        }

        bool CefSharpRenderDelegate::GetViewRect(CefRect& rect)
        {
            if ((IRenderWebBrowser^)_renderWebBrowser == nullptr)
            {
                return false;
            }

            auto screenInfo = _renderWebBrowser->GetScreenInfo();

            rect = CefRect(0, 0, screenInfo.Width, screenInfo.Height);
            return true;
        };

        void CefSharpRenderDelegate::OnPopupShow(bool show)
        {
            _renderWebBrowser->SetPopupIsOpen(show);
        };

        void CefSharpRenderDelegate::OnPopupSize(const CefRect& rect)
        {
            _renderWebBrowser->SetPopupSizeAndPosition(rect.width, rect.height, rect.x, rect.y);
        };

        void CefSharpRenderDelegate::OnPaint(CefRenderHandler::PaintElementType type, const CefRenderHandler::RectList& dirtyRects,
            const void* buffer, int width, int height)
        {
            auto bitmapInfo = type == PET_VIEW ? _mainBitmapInfo : _popupBitmapInfo;

            lock l(bitmapInfo->BitmapLock);

            if (bitmapInfo->DirtyRectSupport)
            {
                //NOTE: According to https://bitbucket.org/chromiumembedded/branches-2171-cef3/commits/ce984ddff3268a50cf9967487327e1257015b98c
                // There is only one rect now that's a union of all dirty regions. API Still passes in a vector

                CefRect r = dirtyRects.front();
                bitmapInfo->DirtyRect = CefDirtyRect(r.x, r.y, r.width, r.height);
            }

            auto backBufferHandle = (HANDLE)bitmapInfo->BackBufferHandle;

            if (backBufferHandle == NULL || bitmapInfo->Width != width || bitmapInfo->Height != height)
            {
                int pixels = width * height;
                int numberOfBytes = pixels * bitmapInfo->BytesPerPixel;
                auto fileMappingHandle = (HANDLE)bitmapInfo->FileMappingHandle;

                //Clear the reference to Bitmap so a new one is created by InvokeRenderAsync
                bitmapInfo->ClearBitmap();

                //Release the current handles (if not null)
                ReleaseBitmapHandlers(bitmapInfo);

                // Create new fileMappingHandle
                fileMappingHandle = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, numberOfBytes, NULL);
                if (fileMappingHandle == NULL)
                {
                    // TODO: Consider doing something more sensible here, since the browser will be very badly broken if this
                    // TODO: method call fails.
                    return;
                }

                backBufferHandle = MapViewOfFile(fileMappingHandle, FILE_MAP_ALL_ACCESS, 0, 0, numberOfBytes);
                if (backBufferHandle == NULL)
                {
                    // TODO: Consider doing something more sensible here, since the browser will be very badly broken if this
                    // TODO: method call fails.
                    return;
                }

                bitmapInfo->FileMappingHandle = (IntPtr)fileMappingHandle;
                bitmapInfo->BackBufferHandle = (IntPtr)backBufferHandle;
                bitmapInfo->Width = width;
                bitmapInfo->Height = height;
                bitmapInfo->NumberOfBytes = numberOfBytes;
            }

            CopyMemory(backBufferHandle, (void*)buffer, bitmapInfo->NumberOfBytes);

            _renderWebBrowser->InvokeRenderAsync(bitmapInfo);
        };

        void CefSharpRenderDelegate::OnCursorChange(CefCursorHandle cursor, CefRenderHandler::CursorType type, const CefCursorInfo& custom_cursor_info)
        {
            _renderWebBrowser->SetCursor((IntPtr)cursor, (CefSharp::CefCursorType)type);
        };
    }
}