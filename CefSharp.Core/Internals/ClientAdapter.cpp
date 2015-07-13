// Copyright © 2010-2015 The CefSharp Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "Stdafx.h"

#include "include/wrapper/cef_stream_resource_handler.h"
#include "ClientAdapter.h"
#include "CefRequestWrapper.h"
#include "CefContextMenuParamsWrapper.h"
#include "CefDragDataWrapper.h"
#include "TypeConversion.h"
#include "CefSharpBrowserWrapper.h"
#include "CefDownloadItemCallbackWrapper.h"
#include "CefBeforeDownloadCallbackWrapper.h"
#include "CefGeolocationCallbackWrapper.h"
#include "CefFileDialogCallbackWrapper.h"
#include "CefAuthCallbackWrapper.h"
#include "CefJSDialogCallbackWrapper.h"
#include "CefRequestCallbackWrapper.h"
#include "CefWindowInfoWrapper.h"
#include "Serialization\Primitives.h"
#include "Serialization\V8Serialization.h"
#include "Messaging\Messages.h"

using namespace CefSharp::Internals::Messaging;
using namespace CefSharp::Internals::Serialization;

namespace CefSharp
{
    namespace Internals
    {
        IBrowser^ ClientAdapter::GetBrowserWrapper(int browserId, bool isPopup)
        {
            IBrowser^ result;
            Tuple<IBrowser^, IBrowserAdapter^, IWebBrowserInternal^, IntPtr>^ browserData;
            if (_browsers->TryGetValue(browserId, browserData))
            {
                result = browserData->Item1;
            }
            else
            {
                auto stackFrame = gcnew StackFrame(1);
                auto callingMethodName = stackFrame->GetMethod()->Name;

                ThrowUnknownPopupBrowser(gcnew String(L"ClientAdapter::" + callingMethodName));
            }
            return result;
        }

        void ClientAdapter::CloseAllPopups(bool forceClose)
        {
            //TODO: how to handle this
            for each(auto browserData in _browsers->Values)
            {
                if (browserData->Item1->IsPopup)
                {
                    browserData->Item1->GetHost()->CloseBrowser(forceClose);
                    // NOTE: We don't dispose the IBrowsers here
                    // because ->CloseBrowser() will invoke
                    // ->OnBeforeClose() for the browser.
                    // OnBeforeClose() disposes the IBrowser there.
                }
            }
        }

        bool ClientAdapter::OnBeforePopup(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, const CefString& target_url,
            const CefString& target_frame_name, const CefPopupFeatures& popupFeatures, CefWindowInfo& windowInfo,
            CefRefPtr<CefClient>& client, CefBrowserSettings& settings, bool* no_javascript_access)
        {
            auto result = false;
            //TODO: might always use the main browser
            auto webBrowser = GetWebBrowser(browser->GetIdentifier());
            auto handler = InternalWebBrowserExtensions::GetLifeSpanHandler(webBrowser);
            IWebBrowserInternal^ pendingPopup = nullptr;

            if (handler != nullptr)
            {
                bool createdWrapper = false;
                IBrowser^ browserWrapper = GetBrowserWrapper(browser->GetIdentifier(), browser->IsPopup());

                CefFrameWrapper frameWrapper(frame);
                CefWindowInfoWrapper windowInfoWrapper(&windowInfo);

                IWebBrowser^ newWebBrowser;
                result = handler->OnBeforePopup(
                    webBrowser, browserWrapper,
                    %frameWrapper, StringUtils::ToClr(target_url),
                    StringUtils::ToClr(target_frame_name),
                    %windowInfoWrapper, *no_javascript_access, newWebBrowser);

                auto webBrowserInternal = dynamic_cast<IWebBrowserInternal^>(newWebBrowser);
                if (!result && webBrowserInternal != nullptr)
                {
                    auto mainBrowserData = _nativeBrowsers[_mainBrowserId];
                    auto windowlessSupported = mainBrowserData->GetHost()->IsWindowRenderingDisabled();

                    auto browserParentHandle = (HWND)webBrowserInternal->ParentHandle.ToPointer();
                    auto isBrowserOffscreen = IRenderWebBrowser::typeid->IsAssignableFrom(webBrowserInternal->GetType());
                    //let's override windowinfo if it wasn't setup properly
                    if (isBrowserOffscreen && windowlessSupported && !windowInfo.windowless_rendering_enabled)
                    {
                        //make sure we have an offscreen browser for wpf
                        windowInfo.SetAsWindowless(NULL, true);
                    }
                    else if (!isBrowserOffscreen && windowInfo.parent_window != browserParentHandle)
                    {
                        //make sure the hwnd is properly set up
                        RECT rect;
                        GetClientRect(browserParentHandle, &rect);
                        rect.right = 100;
                        rect.bottom = 100;
                        windowInfo.SetAsChild(browserParentHandle, rect);
                    }

                    auto browserValid = (isBrowserOffscreen && windowlessSupported) || !isBrowserOffscreen;
                    if (browserValid)
                    {
                        pendingPopup = webBrowserInternal;
                    }
                }
            }

            //OnBeforePopup is always called on the IO thread so we can do this
            _pendingPopups->Enqueue(pendingPopup);

            return result;
        }

        void ClientAdapter::OnAfterCreated(CefRefPtr<CefBrowser> browser)
        {
            //save this here because we need GetCefBrowser to provide meaningful values while dealing with browser adapters
            _nativeBrowsers.emplace(browser->GetIdentifier(), browser);
            auto browsers = static_cast<Dictionary<int, Tuple<IBrowser^, IBrowserAdapter^, IWebBrowserInternal^, IntPtr>^>^>(_browsers);

            Tuple<IBrowser^, IBrowserAdapter^, IWebBrowserInternal^, IntPtr>^ browserData = nullptr;
            if (browser->IsPopup())
            {
                auto webBrowserInternal = _pendingPopups->Dequeue();
                auto browserWrapper = gcnew CefSharpBrowserWrapper(browser, CefRefPtr<ClientAdapter>(this));
                auto mainBrowserData = GetMainBrowserData();
                auto mainBrowser = mainBrowserData->Item3;
                IBrowserAdapter^ browserAdapter = nullptr;

                if (webBrowserInternal != nullptr)
                {
                    webBrowserInternal->SetBrowserAdapter(nullptr);
                    browserAdapter = gcnew ManagedCefBrowserAdapter(webBrowserInternal, browserWrapper);
                    //reassign the browser adapter of the newly created webbrowser
                    webBrowserInternal->SetBrowserAdapter(browserAdapter);
                    browserAdapter->OnAfterBrowserCreated(browser->GetIdentifier());
                    browser->GetHost()->NotifyMoveOrResizeStarted();
                    _javascriptCallbackFactories->Add(browser->GetIdentifier(), browserAdapter->JavascriptCallbackFactory);
                }

                auto handler = mainBrowser->PopupHandler;
                if (handler != nullptr)
                {
                    handler->OnAfterCreated(mainBrowser, browserWrapper, webBrowserInternal);
                }

                browserData = Tuple::Create(static_cast<IBrowser^>(browserWrapper),
                    browserAdapter,
                    webBrowserInternal,
                    IntPtr(browser->GetHost()->GetWindowHandle()));
            }
            else if (!Object::ReferenceEquals(_mainBrowserAdapter, nullptr))
            {
                _mainBrowserId = browser->GetIdentifier();
                auto browserHwnd = IntPtr(browser->GetHost()->GetWindowHandle());

                if (!_mainBrowserAdapter->IsDisposed)
                {
                    _mainBrowserAdapter->OnAfterBrowserCreated(browser->GetIdentifier());
                    //save callback factory for this browser
                    //it's only going to be present after browseradapter is initialized


                    browserData = Tuple::Create(_mainBrowserAdapter->GetBrowser(),
                        static_cast<IBrowserAdapter^>(_mainBrowserAdapter),
                        static_cast<IWebBrowserInternal^>(_mainBrowser),
                        IntPtr(browser->GetHost()->GetWindowHandle()));
                    _javascriptCallbackFactories->Add(browser->GetIdentifier(), _mainBrowserAdapter->JavascriptCallbackFactory);
                }
                else
                {
                    browser->GetHost()->CloseBrowser(true);
                }
            }

            if (browserData != nullptr)
            {
                browsers[browser->GetIdentifier()] = browserData;
            }
        }

        void ClientAdapter::OnBeforeClose(CefRefPtr<CefBrowser> browser)
        {
            auto webBrowser = GetWebBrowser(browser->GetIdentifier());
            if (browser->IsPopup())
            {
                IBrowser^ browserWrapper = GetBrowserWrapper(browser->GetIdentifier(), true);
                auto mainBrowserData = GetMainBrowserData();
                auto handler = mainBrowserData->Item3->PopupHandler;
                if (handler != nullptr)
                {
                    handler->OnBeforeClose(mainBrowserData->Item3, browserWrapper);
                }
            }
            
            if (webBrowser != nullptr && GetBrowserHwnd(browser->GetIdentifier()) == browser->GetHost()->GetWindowHandle())
            {
                auto handler = webBrowser->LifeSpanHandler;
                if (handler != nullptr)
                {
                    IBrowser^ browserWrapper = GetBrowserWrapper(browser->GetIdentifier(), false);

                    handler->OnBeforeClose(webBrowser, browserWrapper);
                }
            }

            _browsers->Remove(browser->GetIdentifier());
            _nativeBrowsers.erase(browser->GetIdentifier());
        }

        void ClientAdapter::OnLoadingStateChange(CefRefPtr<CefBrowser> browser, bool isLoading, bool canGoBack, bool canGoForward)
        {
            auto webBrowser = GetWebBrowser(browser->GetIdentifier());
            if (browser->IsPopup() && webBrowser == nullptr)
            {
                auto mainBrowser = GetMainBrowserData();
                auto handler = mainBrowser->Item3->PopupHandler;
                if (handler != nullptr)
                {
                    IBrowser^ browserWrapper = GetBrowserWrapper(browser->GetIdentifier(), true);

                    handler->OnLoadingStateChange(mainBrowser->Item3, browserWrapper, isLoading, canGoBack, canGoForward);
                }
            }
            else if (webBrowser != nullptr)
            {
                webBrowser->SetLoadingStateChange(canGoBack, canGoForward, isLoading);
            }
        }

        void ClientAdapter::OnAddressChange(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, const CefString& address)
        {
            auto webBrowser = GetWebBrowser(browser->GetIdentifier());
            if (webBrowser != nullptr)
            {
                webBrowser->SetAddress(StringUtils::ToClr(address));
            }
        }

        void ClientAdapter::OnTitleChange(CefRefPtr<CefBrowser> browser, const CefString& title)
        {
            auto webBrowser = GetWebBrowser(browser->GetIdentifier());
            if (browser->IsPopup() && webBrowser == nullptr)
            {
                // Set the popup window title
                auto hwnd = browser->GetHost()->GetWindowHandle();
                SetWindowText(hwnd, std::wstring(title).c_str());
            }
            else if (webBrowser != nullptr)
            {
                webBrowser->SetTitle(StringUtils::ToClr(title));
            }
        }

        void ClientAdapter::OnFaviconURLChange(CefRefPtr<CefBrowser> browser, const std::vector<CefString>& iconUrls)
        {
            auto webBrowser = GetWebBrowser(browser->GetIdentifier());
            if (browser->IsPopup() && webBrowser == nullptr)
            {
                auto mainBrowser = GetMainBrowserData();
                auto popupHandler = mainBrowser->Item3->PopupHandler;
                if (popupHandler != nullptr)
                {
                    IBrowser^ browserWrapper = GetBrowserWrapper(browser->GetIdentifier(), true);

                    popupHandler->OnFaviconUrlChange(mainBrowser->Item3, browserWrapper, StringUtils::ToClr(iconUrls));
                }
            }
            else
            {
                auto handler = InternalWebBrowserExtensions::GetRequestHandler(webBrowser);
                if (handler != nullptr)
                {
                    handler->OnFaviconUrlChange(webBrowser, StringUtils::ToClr(iconUrls));
                }
            }
        }

        bool ClientAdapter::OnTooltip(CefRefPtr<CefBrowser> browser, CefString& text)
        {
            auto webBrowser = GetWebBrowser(browser->GetIdentifier());
            String^ tooltip = StringUtils::ToClr(text);

            // TODO: Deal with popuup browsers properly...
            if (webBrowser != nullptr)
            {
                //_tooltip = tooltip;
                webBrowser->SetTooltipText(_tooltip);
            }

            return true;
        }

        bool ClientAdapter::OnConsoleMessage(CefRefPtr<CefBrowser> browser, const CefString& message, const CefString& source, int line)
        {
            auto webBrowser = GetWebBrowser(browser->GetIdentifier());

            String^ messageStr = StringUtils::ToClr(message);
            String^ sourceStr = StringUtils::ToClr(source);

            if (webBrowser != nullptr)
            {
                webBrowser->OnConsoleMessage(messageStr, sourceStr, line);
            }

            return true;
        }

        void ClientAdapter::OnStatusMessage(CefRefPtr<CefBrowser> browser, const CefString& value)
        {
            auto webBrowser = GetWebBrowser(browser->GetIdentifier());
            auto statusMessage = StringUtils::ToClr(value);

            if (browser->IsPopup() && webBrowser == nullptr)
            {
                auto mainBrowser = GetMainBrowserData();
                auto popupHandler = mainBrowser->Item3->PopupHandler;
                if (popupHandler != nullptr)
                {
                    IBrowser^ browserWrapper = GetBrowserWrapper(browser->GetIdentifier(), true);

                    popupHandler->OnStatusMessage(mainBrowser->Item3, browserWrapper, statusMessage);
                }
            }
            else
            {
                webBrowser->OnStatusMessage(statusMessage);
            }
        }

        bool ClientAdapter::OnKeyEvent(CefRefPtr<CefBrowser> browser, const CefKeyEvent& event, CefEventHandle os_event)
        {
            auto webBrowser = GetWebBrowser(browser->GetIdentifier());

            if (browser->IsPopup() && webBrowser == nullptr)
            {
                auto mainBrowser = GetMainBrowserData();
                auto popupHandler = mainBrowser->Item3->PopupHandler;
                if (popupHandler != nullptr)
                {
                    IBrowser^ browserWrapper = GetBrowserWrapper(browser->GetIdentifier(), true);

                    return popupHandler->OnKeyEvent(
                        mainBrowser->Item3, browserWrapper, (KeyType)event.type,
                        event.windows_key_code, event.native_key_code, 
                        (CefEventFlags)event.modifiers, event.is_system_key == 1);
                }
            }
            else
            {
                auto handler = InternalWebBrowserExtensions::GetKeyboardHandler(webBrowser);

                if (handler == nullptr)
                {
                    return false;
                }

                IBrowser^ browserWrapper = GetBrowserWrapper(browser->GetIdentifier(), false);

                return handler->OnKeyEvent(
                    webBrowser, browserWrapper, (KeyType)event.type, event.windows_key_code,
                    event.native_key_code,
                    (CefEventFlags)event.modifiers, event.is_system_key == 1);
            }
            return false;
        }

        bool ClientAdapter::OnPreKeyEvent(CefRefPtr<CefBrowser> browser, const CefKeyEvent& event, CefEventHandle os_event, bool* is_keyboard_shortcut)
        {
            auto webBrowser = GetWebBrowser(browser->GetIdentifier());

            if (browser->IsPopup() && webBrowser == nullptr)
            {
                auto mainBrowser = GetMainBrowserData();
                auto popupHandler = mainBrowser->Item3->PopupHandler;
                if (popupHandler != nullptr)
                {
                    IBrowser^ browserWrapper = GetBrowserWrapper(browser->GetIdentifier(), true);

                    popupHandler->OnPreKeyEvent(mainBrowser->Item3, browserWrapper, (KeyType)event.type, event.windows_key_code, event.native_key_code, (CefEventFlags)event.modifiers, event.is_system_key == 1, *is_keyboard_shortcut);
                }
            }
            else
            {
                auto handler = InternalWebBrowserExtensions::GetKeyboardHandler(webBrowser);

                if (handler == nullptr)
                {
                    return false;
                }

                IBrowser^ browserWrapper = GetBrowserWrapper(browser->GetIdentifier(), false);

                return handler->OnPreKeyEvent(
                    webBrowser, browserWrapper, (KeyType)event.type, event.windows_key_code,
                    event.native_key_code, (CefEventFlags)event.modifiers, event.is_system_key == 1,
                    *is_keyboard_shortcut);
            }
            return false;
        }

        void ClientAdapter::OnLoadStart(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame)
        {
            auto webBrowser = GetWebBrowser(browser->GetIdentifier());

            if (browser->IsPopup() && webBrowser == nullptr)
            {
                auto mainBrowser = GetMainBrowserData();
                auto popupHandler = mainBrowser->Item3->PopupHandler;
                if (popupHandler != nullptr)
                {
                    IBrowser^ browserWrapper = GetBrowserWrapper(browser->GetIdentifier(), true);
                    CefFrameWrapper frameWrapper(frame);
                    popupHandler->OnFrameLoadStart(mainBrowser->Item3, gcnew FrameLoadStartEventArgs(browserWrapper, %frameWrapper));
                }
            }
            else
            {
                IBrowser^ browserWrapper = GetBrowserWrapper(browser->GetIdentifier(), false);
                CefFrameWrapper frameWrapper(frame);
                webBrowser->OnFrameLoadStart(gcnew FrameLoadStartEventArgs(browserWrapper, %frameWrapper));
            }
        }

        void ClientAdapter::OnLoadEnd(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int httpStatusCode)
        {
            auto webBrowser = GetWebBrowser(browser->GetIdentifier());

            if (browser->IsPopup() && webBrowser == nullptr)
            {
                auto mainBrowser = GetMainBrowserData();
                auto popupHandler = mainBrowser->Item3->PopupHandler;
                if (popupHandler != nullptr)
                {
                    IBrowser^ browserWrapper = GetBrowserWrapper(browser->GetIdentifier(), true);

                    CefFrameWrapper frameWrapper(frame);
                    popupHandler->OnFrameLoadEnd(mainBrowser->Item3, gcnew FrameLoadEndEventArgs(browserWrapper, %frameWrapper, httpStatusCode));
                }
            }
            else
            {
                CefFrameWrapper frameWrapper(frame);
                auto browserWrapper = GetBrowserWrapper(browser->GetIdentifier(), false);
                webBrowser->OnFrameLoadEnd(gcnew FrameLoadEndEventArgs(browserWrapper, %frameWrapper, httpStatusCode));
            }
        }

        void ClientAdapter::OnLoadError(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, ErrorCode errorCode, const CefString& errorText, const CefString& failedUrl)
        {
            auto webBrowser = GetWebBrowser(browser->GetIdentifier());

            if (browser->IsPopup() && webBrowser == nullptr)
            {
                auto mainBrowser = GetMainBrowserData();
                auto popupHandler = mainBrowser->Item3->PopupHandler;
                if (popupHandler != nullptr)
                {
                    IBrowser^ browserWrapper = GetBrowserWrapper(browser->GetIdentifier(), true);
                    CefFrameWrapper frameWrapper(frame);
                    popupHandler->OnLoadError(mainBrowser->Item3, browserWrapper,
                        gcnew LoadErrorEventArgs(%frameWrapper, (CefErrorCode)errorCode, StringUtils::ToClr(errorText), StringUtils::ToClr(failedUrl)));
                }
            }
            else
            {
                CefFrameWrapper frameWrapper(frame);
                webBrowser->OnLoadError(%frameWrapper, (CefErrorCode)errorCode, StringUtils::ToClr(errorText), StringUtils::ToClr(failedUrl));
            }
        }

        bool ClientAdapter::OnBeforeBrowse(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefRequest> request, bool isRedirect)
        {
            auto webBrowser = GetWebBrowser(browser->GetIdentifier());

            if (browser->IsPopup() && webBrowser == nullptr)
            {
                auto mainBrowser = GetMainBrowserData();
                auto popupHandler = mainBrowser->Item3->PopupHandler;
                if (popupHandler == nullptr)
                {
                    return false;
                }

                IBrowser^ browserWrapper = GetBrowserWrapper(browser->GetIdentifier(), true);

                CefFrameWrapper frameWrapper(frame);
                CefRequestWrapper requestWrapper(request);
                return popupHandler->OnBeforeBrowse(mainBrowser->Item3, browserWrapper, %requestWrapper, isRedirect, %frameWrapper);
            }
            else
            {
                auto handler = InternalWebBrowserExtensions::GetRequestHandler(webBrowser);
                if (handler == nullptr)
                {
                    return false;
                }

                IBrowser^ browserWrapper = GetBrowserWrapper(browser->GetIdentifier(), false);
                CefFrameWrapper frameWrapper(frame);
                CefRequestWrapper requestWrapper(request);
                
                return handler->OnBeforeBrowse(webBrowser, browserWrapper, %frameWrapper, %requestWrapper, isRedirect);
            }

            return false;
        }

        bool ClientAdapter::OnCertificateError(CefRefPtr<CefBrowser> browser, cef_errorcode_t cert_error, const CefString& request_url, CefRefPtr<CefSSLInfo> ssl_info, CefRefPtr<CefRequestCallback> callback)
        {
            auto webBrowser = GetWebBrowser(browser->GetIdentifier());
            auto handler = InternalWebBrowserExtensions::GetRequestHandler(webBrowser);
            if (handler == nullptr)
            {
                return false;
            }

            // If callback is empty the error cannot be recovered from and the request will be canceled automatically.
            // Still notify the user of the certificate error just don't provide a callback.
            auto requestCallback = callback == NULL ? nullptr : gcnew CefRequestCallbackWrapper(callback);

            IBrowser^ browserWrapper = GetBrowserWrapper(browser->GetIdentifier(), browser->IsPopup());

            return handler->OnCertificateError(webBrowser, browserWrapper, (CefErrorCode)cert_error, StringUtils::ToClr(request_url), requestCallback);
        }

        bool ClientAdapter::OnQuotaRequest(CefRefPtr<CefBrowser> browser, const CefString& originUrl, int64 newSize, CefRefPtr<CefRequestCallback> callback)
        {
            auto webBrowser = GetWebBrowser(browser->GetIdentifier());
            auto handler = InternalWebBrowserExtensions::GetRequestHandler(webBrowser);
            if (handler == nullptr)
            {
                return false;
            }
            
            auto requestCallback = gcnew CefRequestCallbackWrapper(callback);

            IBrowser^ browserWrapper = GetBrowserWrapper(browser->GetIdentifier(), browser->IsPopup());

            return handler->OnQuotaRequest(webBrowser, browserWrapper, StringUtils::ToClr(originUrl), newSize, requestCallback);
        }

        // CEF3 API: public virtual bool OnBeforePluginLoad( CefRefPtr< CefBrowser > browser, const CefString& url, const CefString& policy_url, CefRefPtr< CefWebPluginInfo > info );
        // ---
        // return value:
        //     false: Load Plugin (do not block it)
        //     true:  Ignore Plugin (Block it)
        bool ClientAdapter::OnBeforePluginLoad(CefRefPtr<CefBrowser> browser, const CefString& url, const CefString& policy_url, CefRefPtr<CefWebPluginInfo> info)
        {
            auto webBrowser = GetWebBrowser(browser->GetIdentifier());
            auto handler = InternalWebBrowserExtensions::GetRequestHandler(webBrowser);

            if (handler == nullptr)
            {
                return false;
            }

            auto pluginInfo = TypeConversion::FromNative(info);

            IBrowser^ browserWrapper = GetBrowserWrapper(browser->GetIdentifier(), browser->IsPopup());

            return handler->OnBeforePluginLoad(webBrowser, browserWrapper, StringUtils::ToClr(url), StringUtils::ToClr(policy_url), pluginInfo);
        }

        void ClientAdapter::OnPluginCrashed(CefRefPtr<CefBrowser> browser, const CefString& plugin_path)
        {
            auto webBrowser = GetWebBrowser(browser->GetIdentifier());
            auto handler = InternalWebBrowserExtensions::GetRequestHandler(webBrowser);

            if (handler != nullptr)
            {
                IBrowser^ browserWrapper = GetBrowserWrapper(browser->GetIdentifier(), browser->IsPopup());

                handler->OnPluginCrashed(webBrowser, browserWrapper, StringUtils::ToClr(plugin_path));
            }
        }

        void ClientAdapter::OnRenderProcessTerminated(CefRefPtr<CefBrowser> browser, TerminationStatus status)
        {
            auto webBrowser = GetWebBrowser(browser->GetIdentifier());
            auto handler = InternalWebBrowserExtensions::GetRequestHandler(webBrowser);

            if (handler != nullptr)
            {
                IBrowser^ browserWrapper = GetBrowserWrapper(browser->GetIdentifier(), browser->IsPopup());

                handler->OnRenderProcessTerminated(webBrowser, browserWrapper, (CefTerminationStatus)status);
            }
        }

        void ClientAdapter::OnResourceRedirect(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, const CefString& oldUrl, CefString& newUrl)
        {
            auto webBrowser = GetWebBrowser(browser->GetIdentifier());
            if (browser->IsPopup() && webBrowser == nullptr)
            {
                auto mainBrowser = GetMainBrowserData();
                auto popupHandler = mainBrowser->Item3->PopupHandler;
                if (popupHandler != nullptr)
                {
                    auto managedNewUrl = StringUtils::ToClr(newUrl);
                    IBrowser^ browserWrapper = GetBrowserWrapper(browser->GetIdentifier(), true);;
                    CefFrameWrapper frameWrapper(frame);

                    popupHandler->OnResourceRedirect(mainBrowser->Item3, browserWrapper, %frameWrapper, managedNewUrl);

                    newUrl = StringUtils::ToNative(managedNewUrl);
                }
            }
            else
            {
                auto handler = InternalWebBrowserExtensions::GetRequestHandler(webBrowser);
                if (handler != nullptr)
                {
                    auto managedNewUrl = StringUtils::ToClr(newUrl);
                    CefFrameWrapper frameWrapper(frame);

                    handler->OnResourceRedirect(webBrowser, %frameWrapper, managedNewUrl);

                    newUrl = StringUtils::ToNative(managedNewUrl);
                }
            }
        }

        void ClientAdapter::OnProtocolExecution(CefRefPtr<CefBrowser> browser, const CefString& url, bool& allowOSExecution)
        {
            auto webBrowser = GetWebBrowser(browser->GetIdentifier());
            auto handler = InternalWebBrowserExtensions::GetRequestHandler(webBrowser);

            if (handler != nullptr)
            {
                IBrowser^ browserWrapper = GetBrowserWrapper(browser->GetIdentifier(), browser->IsPopup());

                allowOSExecution = handler->OnProtocolExecution(webBrowser, browserWrapper, StringUtils::ToClr(url));
            }
        }

        // Called on the IO thread before a resource is loaded. To allow the resource
        // to load normally return NULL. To specify a handler for the resource return
        // a CefResourceHandler object. The |request| object should not be modified in
        // this callback.
        CefRefPtr<CefResourceHandler> ClientAdapter::GetResourceHandler(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefRequest> request)
        {
            auto webBrowser = GetWebBrowser(browser->GetIdentifier());
            auto factory = InternalWebBrowserExtensions::GetResourceHandlerFactory(webBrowser);

            if (factory == nullptr || !factory->HasHandlers)
            {
                return NULL;
            }

            IBrowser^ browserWrapper = GetBrowserWrapper(browser->GetIdentifier(), browser->IsPopup());
            auto frameWrapper = gcnew CefFrameWrapper(frame);
            auto requestWrapper = gcnew CefRequestWrapper(request);

            auto resourceHandler = factory->GetResourceHandler(webBrowser, browserWrapper, frameWrapper, requestWrapper);

            if (resourceHandler == nullptr)
            {
                // Clean up our disposables if our factory doesn't want
                // this request.
                delete frameWrapper;
                delete requestWrapper;
                return NULL;
            }

            // No need to pass browserWrapper for disposable lifetime management here
            // because GetBrowserWrapper returned IBrowser^s are already properly
            // managed.
            return new ResourceHandlerWrapper(resourceHandler, nullptr, frameWrapper, requestWrapper);
        }

        cef_return_value_t ClientAdapter::OnBeforeResourceLoad(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefRequest> request, CefRefPtr<CefRequestCallback> callback)
        {
            auto webBrowser = GetWebBrowser(browser->GetIdentifier());
            auto handler = InternalWebBrowserExtensions::GetRequestHandler(webBrowser);

            if (handler == nullptr)
            {
                return cef_return_value_t::RV_CONTINUE;
            }

           /* if (browser->IsPopup())
            {
                auto popupHandler = webBrowser->PopupHandler;
                if (popupHandler != nullptr)
                {
                    auto frameWrapper = gcnew CefFrameWrapper(frame);
                    auto requestWrapper = gcnew CefRequestWrapper(request);
                    IBrowser^ browserWrapper = GetBrowserWrapper(browser->GetIdentifier(), true);

                    auto requestCallback = gcnew CefRequestCallbackWrapper(callback, frameWrapper, requestWrapper);

                    return (cef_return_value_t)popupHandler->OnBeforeResourceLoad(webBrowser, browserWrapper, frameWrapper, requestWrapper, requestCallback);
                }
            }
            else
            {*/
                auto frameWrapper = gcnew CefFrameWrapper(frame);
                auto browserWrapper = GetBrowserWrapper(browser->GetIdentifier(), false);
                auto requestWrapper = gcnew CefRequestWrapper(request);
                auto requestCallback = gcnew CefRequestCallbackWrapper(callback, frameWrapper, requestWrapper);

                return (cef_return_value_t)handler->OnBeforeResourceLoad(webBrowser, browserWrapper, frameWrapper, requestWrapper, requestCallback);
            //}
            //return cef_return_value_t::RV_CONTINUE;
        }

        bool ClientAdapter::GetAuthCredentials(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, bool isProxy,
            const CefString& host, int port, const CefString& realm, const CefString& scheme, CefRefPtr<CefAuthCallback> callback)
        {
            auto webBrowser = GetWebBrowser(browser->GetIdentifier());
            auto handler = InternalWebBrowserExtensions::GetRequestHandler(webBrowser);
            if (handler == nullptr)
            {
                return false;
            }

            auto browserWrapper = GetBrowserWrapper(browser->GetIdentifier(), browser->IsPopup());
            auto frameWrapper = gcnew CefFrameWrapper(frame);
            auto callbackWrapper = gcnew CefAuthCallbackWrapper(callback, frameWrapper);

            return handler->GetAuthCredentials(
                webBrowser, browserWrapper, frameWrapper, isProxy,
                StringUtils::ToClr(host), port, StringUtils::ToClr(realm), 
                StringUtils::ToClr(scheme), callbackWrapper);
        }

        void ClientAdapter::OnBeforeContextMenu(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
            CefRefPtr<CefContextMenuParams> params, CefRefPtr<CefMenuModel> model)
        {
            auto webBrowser = GetWebBrowser(browser->GetIdentifier());
            auto handler = InternalWebBrowserExtensions::GetMenuHandler(webBrowser);
            if (handler == nullptr) return;

            // Context menu params
            CefContextMenuParamsWrapper contextMenuParamsWrapper(params);
            CefFrameWrapper frameWrapper(frame);
            auto result = handler->OnBeforeContextMenu(webBrowser, %frameWrapper, %contextMenuParamsWrapper);
            if (!result)
            {
                model->Clear();
            }
        }

        void ClientAdapter::OnGotFocus(CefRefPtr<CefBrowser> browser)
        {
            auto handler = InternalWebBrowserExtensions::GetFocusHandler(GetWebBrowser(browser->GetIdentifier()));

            if (handler == nullptr)
            {
                return;
            }

            // NOTE: a popup handler for OnGotFocus doesn't make sense yet because
            // non-offscreen windows don't wrap popup browser's yet.
            if (!browser->IsPopup())
            {
                handler->OnGotFocus();
            }
        }

        bool ClientAdapter::OnSetFocus(CefRefPtr<CefBrowser> browser, FocusSource source)
        {
            auto handler = InternalWebBrowserExtensions::GetFocusHandler(GetWebBrowser(browser->GetIdentifier()));

            if (handler == nullptr)
            {
                // Allow the focus to be set by default.
                return false;
            }

            // NOTE: a popup handler for OnGotFocus doesn't make sense yet because
            // non-offscreen windows don't wrap popup browser's yet.
            if (!browser->IsPopup())
            {
                return handler->OnSetFocus((CefFocusSource)source);
            }
            // Allow the focus to be set by default.
            return false;
        }

        void ClientAdapter::OnTakeFocus(CefRefPtr<CefBrowser> browser, bool next)
        {
            auto handler = InternalWebBrowserExtensions::GetFocusHandler(GetWebBrowser(browser->GetIdentifier()));

            if (handler == nullptr)
            {
                return;
            }

            // NOTE: a popup handler for OnGotFocus doesn't make sense yet because
            // non-offscreen windows don't wrap popup browser's yet.
            if (!browser->IsPopup())
            {
                handler->OnTakeFocus(next);
            }
        }

        bool ClientAdapter::OnJSDialog(CefRefPtr<CefBrowser> browser, const CefString& origin_url, const CefString& accept_lang,
            JSDialogType dialog_type, const CefString& message_text, const CefString& default_prompt_text,
            CefRefPtr<CefJSDialogCallback> callback, bool& suppress_message)
        {
            auto webBrowser = GetWebBrowser(browser->GetIdentifier());
            auto handler = InternalWebBrowserExtensions::GetJsDialogHandler(webBrowser);

            if (handler == nullptr)
            {
                return false;
            }

            auto browserWrapper = GetBrowserWrapper(browser->GetIdentifier(), browser->IsPopup());
            auto callbackWrapper = gcnew CefJSDialogCallbackWrapper(callback);
            return handler->OnJSDialog(webBrowser, browserWrapper,
                                       StringUtils::ToClr(origin_url), StringUtils::ToClr(accept_lang), (CefJsDialogType)dialog_type, 
                                       StringUtils::ToClr(message_text), StringUtils::ToClr(default_prompt_text), callbackWrapper, suppress_message);
        }

        bool ClientAdapter::OnBeforeUnloadDialog(CefRefPtr<CefBrowser> browser, const CefString& message_text, bool is_reload, CefRefPtr<CefJSDialogCallback> callback)
        {
            auto webBrowser = GetWebBrowser(browser->GetIdentifier());
            auto handler = InternalWebBrowserExtensions::GetJsDialogHandler(webBrowser);

            if (handler == nullptr)
            {
                return false;
            }

            auto browserWrapper = GetBrowserWrapper(browser->GetIdentifier(), browser->IsPopup());
            auto callbackWrapper = gcnew CefJSDialogCallbackWrapper(callback);

            return handler->OnJSBeforeUnload(webBrowser, browserWrapper, StringUtils::ToClr(message_text), is_reload, callbackWrapper);
        }

        bool ClientAdapter::OnFileDialog(CefRefPtr<CefBrowser> browser, FileDialogMode mode, const CefString& title,
                const CefString& default_file_path, const std::vector<CefString>& accept_filters, int selected_accept_filter,
                CefRefPtr<CefFileDialogCallback> callback)
        {
            auto webBrowser = GetWebBrowser(browser->GetIdentifier());
            auto handler = InternalWebBrowserExtensions::GetDialogHandler(webBrowser);

            if (handler == nullptr)
            {
                return false;
            }

            auto browserWrapper = GetBrowserWrapper(browser->GetIdentifier(), browser->IsPopup());
            auto callbackWrapper = gcnew CefFileDialogCallbackWrapper(callback);

            return handler->OnFileDialog(webBrowser, browserWrapper, (CefFileDialogMode)mode, StringUtils::ToClr(title), StringUtils::ToClr(default_file_path), StringUtils::ToClr(accept_filters), selected_accept_filter, callbackWrapper);
        }

        bool ClientAdapter::OnDragEnter(CefRefPtr<CefBrowser> browser, CefRefPtr<CefDragData> dragData, DragOperationsMask mask)
        {
            auto webBrowser = GetWebBrowser(browser->GetIdentifier());
            auto handler = InternalWebBrowserExtensions::GetDragHandler(webBrowser);

            if (handler == nullptr)
            {
                return false;
            }

            CefDragDataWrapper dragDataWrapper(dragData);
            auto browserWrapper = GetBrowserWrapper(browser->GetIdentifier(), browser->IsPopup());

            return handler->OnDragEnter(webBrowser, browserWrapper, %dragDataWrapper, (CefSharp::DragOperationsMask)mask);
        }

        bool ClientAdapter::OnRequestGeolocationPermission(CefRefPtr<CefBrowser> browser, const CefString& requesting_url, int request_id, CefRefPtr<CefGeolocationCallback> callback)
        {
            auto webBrowser = GetWebBrowser(browser->GetIdentifier());
            auto handler = InternalWebBrowserExtensions::GetGeolocationHandler(webBrowser);
            if (handler == nullptr)
            {
                // Default deny, as CEF does.
                return false;
            }

            auto browserWrapper = GetBrowserWrapper(browser->GetIdentifier(), browser->IsPopup());
            auto callbackWrapper = gcnew CefGeolocationCallbackWrapper(callback);

            return handler->OnRequestGeolocationPermission(webBrowser, browserWrapper, StringUtils::ToClr(requesting_url), request_id, callbackWrapper);
        }

        void ClientAdapter::OnCancelGeolocationPermission(CefRefPtr<CefBrowser> browser, const CefString& requesting_url, int request_id)
        {
            auto webBrowser = GetWebBrowser(browser->GetIdentifier());
            auto handler = InternalWebBrowserExtensions::GetGeolocationHandler(webBrowser);

            if (handler != nullptr)
            {
                auto browserWrapper = GetBrowserWrapper(browser->GetIdentifier(), browser->IsPopup());
                handler->OnCancelGeolocationPermission(webBrowser, browserWrapper, StringUtils::ToClr(requesting_url), request_id);
            }
        }

        void ClientAdapter::OnBeforeDownload(CefRefPtr<CefBrowser> browser, CefRefPtr<CefDownloadItem> download_item,
            const CefString& suggested_name, CefRefPtr<CefBeforeDownloadCallback> callback)
        {
            auto handler = InternalWebBrowserExtensions::GetDownloadHandler(GetWebBrowser(browser->GetIdentifier()));
            
            if(handler != nullptr)
            {
                auto downloadItem = TypeConversion::FromNative(download_item);
                downloadItem->SuggestedFileName = StringUtils::ToClr(suggested_name);

                auto callbackWrapper = gcnew CefBeforeDownloadCallbackWrapper(callback);
                auto browserWrapper = GetBrowserWrapper(browser->GetIdentifier(), browser->IsPopup());

                handler->OnBeforeDownload(browserWrapper, downloadItem, callbackWrapper);
            }
        };

        void ClientAdapter::OnDownloadUpdated(CefRefPtr<CefBrowser> browser, CefRefPtr<CefDownloadItem> download_item,
            CefRefPtr<CefDownloadItemCallback> callback)
        {
            auto handler = InternalWebBrowserExtensions::GetDownloadHandler(GetWebBrowser(browser->GetIdentifier()));

            if(handler != nullptr)
            {
                auto callbackWrapper = gcnew CefDownloadItemCallbackWrapper(callback);
                auto browserWrapper = GetBrowserWrapper(browser->GetIdentifier(), browser->IsPopup());

                handler->OnDownloadUpdated(browserWrapper, TypeConversion::FromNative(download_item), callbackWrapper);
            }
        }


        bool ClientAdapter::OnProcessMessageReceived(CefRefPtr<CefBrowser> browser, CefProcessId source_process, CefRefPtr<CefProcessMessage> message)
        {
            auto handled = false;
            auto name = message->GetName();
            if (name == kEvaluateJavascriptResponse || name == kJavascriptCallbackResponse)
            {
                auto argList = message->GetArgumentList();
                auto success = argList->GetBool(0);
                auto callbackId = GetInt64(argList, 1);

                IJavascriptCallbackFactory^ callbackFactory;
                _javascriptCallbackFactories->TryGetValue(browser->GetIdentifier(), callbackFactory);

                auto pendingTask = _pendingTaskRepository->RemovePendingTask(callbackId);
                if (pendingTask != nullptr)
                {
                    auto response = gcnew JavascriptResponse();
                    response->Success = success;

                    if (success)
                    {
                        response->Result = DeserializeV8Object(argList, 2, callbackFactory);
                    }
                    else
                    {
                        response->Message = StringUtils::ToClr(argList->GetString(2));
                    }

                    pendingTask->SetResult(response);
                }

                handled = true;
            }

            return handled;
        }

        Task<JavascriptResponse^>^ ClientAdapter::EvaluateScriptAsync(int browserId, bool isBrowserPopup, int64 frameId, String^ script, Nullable<TimeSpan> timeout)
        {
            //create a new taskcompletionsource
            auto idAndComplectionSource = _pendingTaskRepository->CreatePendingTask(timeout);

            auto message = CefProcessMessage::Create(kEvaluateJavascriptRequest);
            auto argList = message->GetArgumentList();
            SetInt64(frameId, argList, 0);
            SetInt64(idAndComplectionSource.Key, argList, 1);
            argList->SetString(2, StringUtils::ToNative(script));

            auto browserWrapper = static_cast<CefSharpBrowserWrapper^>(GetBrowserWrapper(browserId, isBrowserPopup));

            browserWrapper->SendProcessMessage(CefProcessId::PID_RENDERER, message);

            return idAndComplectionSource.Value->Task;
        }

        PendingTaskRepository<JavascriptResponse^>^ ClientAdapter::GetPendingTaskRepository()
        {
            return _pendingTaskRepository;
        }

        HWND ClientAdapter::GetBrowserHwnd(int browserId)
        {
            HWND result;
            Tuple<IBrowser^, IBrowserAdapter^, IWebBrowserInternal^, IntPtr>^ browserData;
            if (_browsers->TryGetValue(browserId, browserData))
            {
                result = static_cast<HWND>(browserData->Item4.ToPointer());
            }
            return result;
        }

        CefRefPtr<CefBrowser> ClientAdapter::GetCefBrowser(int browserId)
        {
            CefRefPtr<CefBrowser> result;
            if (_nativeBrowsers.count(browserId) == 1)
            {
                result = _nativeBrowsers[browserId];
            }
            return result;
        }

        IWebBrowserInternal^ ClientAdapter::GetWebBrowser(int browserId)
        {
            IWebBrowserInternal^ result;
            Tuple<IBrowser^, IBrowserAdapter^, IWebBrowserInternal^, IntPtr>^ browserData;
            if (_browsers->TryGetValue(browserId, browserData))
            {
                result = browserData->Item3;
            }
            return result;
        }
    }
}
