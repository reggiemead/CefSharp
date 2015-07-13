using System;
using System.Collections.Generic;
using System.Windows;

namespace CefSharp.Wpf.Example.Handlers
{
    public class NewWindowPopupHandler : IPopupHandler
    {
        private readonly Dictionary<int, Window> windows = new Dictionary<int, Window>();

        public bool OnPreKeyEvent(IWebBrowser browserControl, IBrowser browser, KeyType type, int windowsKeyCode, int nativeKeyCode, CefEventFlags modifiers, bool isSystemKey, ref bool isKeyboardShortcut)
        {
            return false;
        }

        public bool OnKeyEvent(IWebBrowser browserControl, IBrowser browser, KeyType type, int windowsKeyCode, int nativeKeyCode, CefEventFlags modifiers, bool isSystemKey)
        {
            return false;
        }

        public void OnBeforeClose(IWebBrowser browserControl, IBrowser browser)
        {
            Window window;
            if (windows.TryGetValue(browser.Identifier, out window))
            {
                windows.Remove(browser.Identifier);
                window.Close();
            }
        }

        public void OnAfterCreated(IWebBrowser browserControl, IBrowser browser, IWebBrowser webBrowser)
        {
            Application.Current.Dispatcher.BeginInvoke(new Action(() =>
            {
                var browserId = browser.Identifier;
                var window = new Window
                {
                    Content = webBrowser as FrameworkElement
                };
                windows.Add(browserId, window);
                window.Show();
            }));
        }

        public void OnFaviconUrlChange(IWebBrowser browserControl, IBrowser browser, List<string> iconUrls)
        {
        }

        public void OnLoadingStateChange(IWebBrowser browserControl, IBrowser browser, bool isLoading, bool canGoBack, bool canGoForward)
        {
        }

        public void OnStatusMessage(IWebBrowser browserControl, IBrowser browser, string message)
        {
        }

        public void OnFrameLoadStart(IWebBrowser browserControl, FrameLoadStartEventArgs frameLoadStartArgs)
        {
        }

        public void OnFrameLoadEnd(IWebBrowser browserControl, FrameLoadEndEventArgs frameLoadEndArgs)
        {
        }

        public void OnLoadError(IWebBrowser browserControl, IBrowser browser, LoadErrorEventArgs loadErrorArgs)
        {
        }

        public bool OnBeforeBrowse(IWebBrowser browserControl, IBrowser browser, IRequest request, bool isRedirect, IFrame frame)
        {
            return false;
        }

        public void OnResourceRedirect(IWebBrowser browserControl, IBrowser browser, IFrame frame, ref string newUrl)
        {
        }

        public CefReturnValue OnBeforeResourceLoad(IWebBrowser browserControl, IBrowser browser, IFrame frame, IRequest request, IRequestCallback callback)
        {
            return CefReturnValue.Continue;
        }
    }
}
