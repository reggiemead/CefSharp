// Copyright © 2010-2015 The CefSharp Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#pragma once

#include "Stdafx.h"
#include "ClientAdapter.h"
#include "CefSharpRenderDelegate.h"

using namespace msclr;
using namespace System;

namespace CefSharp
{
    namespace Internals
    {
        private class RenderClientAdapter : public ClientAdapter,
            public CefRenderHandler
        {
        private:
            gcroot<Dictionary<int, CefSharpRenderDelegate^>^> _renderDelegates;

        public:
            RenderClientAdapter(IWebBrowserInternal^ webBrowserInternal, IBrowserAdapter^ browserAdapter):
                ClientAdapter(webBrowserInternal, browserAdapter),
                _renderDelegates(gcnew Dictionary<int, CefSharpRenderDelegate^>())
            {
            }

            // CefClient
            virtual DECL CefRefPtr<CefRenderHandler> GetRenderHandler() OVERRIDE{ return this; };

            virtual DECL void OnAfterCreated(CefRefPtr<CefBrowser> browser) OVERRIDE
            {
                ClientAdapter::OnAfterCreated(browser);

                if (!browser->IsPopup())
                {
                    _renderDelegates->Add(browser->GetIdentifier(), 
                        gcnew CefSharpRenderDelegate(
                            static_cast<IRenderWebBrowser^>(GetWebBrowser(browser->GetIdentifier()))));
                }
            }

            virtual DECL void OnBeforeClose(CefRefPtr<CefBrowser> browser) OVERRIDE
            {
                ClientAdapter::OnBeforeClose(browser);
                _renderDelegates->Remove(browser->GetIdentifier());
            }

            // CefRenderHandler
            virtual DECL bool GetScreenInfo(CefRefPtr<CefBrowser> browser, CefScreenInfo& screen_info) OVERRIDE
            {
                bool result = false;
                CefSharpRenderDelegate^ renderDelegate;
                if (_renderDelegates->TryGetValue(browser->GetIdentifier(), renderDelegate))
                {
                    result = renderDelegate->GetScreenInfo(screen_info);
                }
                return result;
            }

            // CefRenderHandler
            virtual DECL bool GetViewRect(CefRefPtr<CefBrowser> browser, CefRect& rect) OVERRIDE
            {
                bool result = false;
                CefSharpRenderDelegate^ renderDelegate;
                if (_renderDelegates->TryGetValue(browser->GetIdentifier(), renderDelegate))
                {
                    result = renderDelegate->GetViewRect(rect);
                }
                return result;
            };

            ///
            // Called when the browser wants to show or hide the popup widget. The popup
            // should be shown if |show| is true and hidden if |show| is false.
            ///
            /*--cef()--*/
            virtual DECL void OnPopupShow(CefRefPtr<CefBrowser> browser, bool show) OVERRIDE
            {
                CefSharpRenderDelegate^ renderDelegate;
                if (_renderDelegates->TryGetValue(browser->GetIdentifier(), renderDelegate))
                {
                    renderDelegate->OnPopupShow(show);
                }
            };

            ///
            // Called when the browser wants to move or resize the popup widget. |rect|
            // contains the new location and size.
            ///
            /*--cef()--*/
            virtual DECL void OnPopupSize(CefRefPtr<CefBrowser> browser, const CefRect& rect) OVERRIDE
            {
                CefSharpRenderDelegate^ renderDelegate;
                if (_renderDelegates->TryGetValue(browser->GetIdentifier(), renderDelegate))
                {
                    renderDelegate->OnPopupSize(rect);
                }
            };

            virtual DECL void OnPaint(CefRefPtr<CefBrowser> browser, PaintElementType type, const RectList& dirtyRects,
                const void* buffer, int width, int height) OVERRIDE
            {
                CefSharpRenderDelegate^ renderDelegate;
                if (_renderDelegates->TryGetValue(browser->GetIdentifier(), renderDelegate))
                {
                    renderDelegate->OnPaint(type, dirtyRects, buffer, width, height);
                }
            };

            virtual DECL void OnCursorChange(CefRefPtr<CefBrowser> browser, CefCursorHandle cursor, CursorType type,
                const CefCursorInfo& custom_cursor_info) OVERRIDE
            {
                CefSharpRenderDelegate^ renderDelegate;
                if (_renderDelegates->TryGetValue(browser->GetIdentifier(), renderDelegate))
                {
                    renderDelegate->OnCursorChange(cursor, type, custom_cursor_info);
                }
            };

            IMPLEMENT_REFCOUNTING(RenderClientAdapter)
        };
    }
}
