// Copyright © 2010-2015 The CefSharp Project. All rights reserved.
//
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#pragma once

#include <include/cef_render_handler.h>

namespace CefSharp
{
    namespace Internals
    {
        private ref class CefSharpRenderDelegate
        {
        private:
            initonly IRenderWebBrowser^ _renderWebBrowser;
            initonly BitmapInfo^ _mainBitmapInfo;
            initonly BitmapInfo^ _popupBitmapInfo;
        
            void ReleaseBitmapHandlers(BitmapInfo^ bitmapInfo)
            {
                auto backBufferHandle = (HANDLE)bitmapInfo->BackBufferHandle;
                auto fileMappingHandle = (HANDLE)bitmapInfo->FileMappingHandle;

                if (backBufferHandle != NULL)
                {
                    UnmapViewOfFile(backBufferHandle);
                    backBufferHandle = NULL;
                    bitmapInfo->BackBufferHandle = IntPtr::Zero;
                }

                if (fileMappingHandle != NULL)
                {
                    CloseHandle(fileMappingHandle);
                    fileMappingHandle = NULL;
                    bitmapInfo->FileMappingHandle = IntPtr::Zero;
                }
            }
        public:
            CefSharpRenderDelegate(IRenderWebBrowser^ renderWebBrowser)
                :_renderWebBrowser(renderWebBrowser)
            {
                _mainBitmapInfo = _renderWebBrowser->CreateBitmapInfo(false);
                _popupBitmapInfo = _renderWebBrowser->CreateBitmapInfo(true);
            }

            bool GetScreenInfo(CefScreenInfo& screen_info);
            bool GetViewRect(CefRect& rect);
            void OnPopupShow(bool show);
            void OnPopupSize(const CefRect& rect);
            void OnPaint(CefRenderHandler::PaintElementType type, const CefRenderHandler::RectList& dirtyRects, const void* buffer, int width, int height);
            void OnCursorChange(CefCursorHandle cursor, CefRenderHandler::CursorType type, const CefCursorInfo& custom_cursor_info);

            !CefSharpRenderDelegate()
            {
                ReleaseBitmapHandlers(_mainBitmapInfo);
                ReleaseBitmapHandlers(_popupBitmapInfo);
            }

            ~CefSharpRenderDelegate()
            {
                this->!CefSharpRenderDelegate();
            }
        };
    }
}