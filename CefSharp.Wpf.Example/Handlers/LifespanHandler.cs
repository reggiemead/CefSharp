using System;
using System.Windows;

namespace CefSharp.Wpf.Example.Handlers
{
    public class LifespanHandler : ILifeSpanHandler
    {
        public bool OnBeforePopup(IWebBrowser browserControl, IBrowser browser, IFrame frame, string targetUrl, string targetFrameName, IWindowInfo windowInfo, ref bool noJavascriptAccess, out IWebBrowser webBrowser)
        {
            IWebBrowser result = null;
            Application.Current.Dispatcher.Invoke(new Action(() =>
            {
                result = new ChromiumWebBrowser();
            }));
            webBrowser = result;
            return false;
        }

        public void OnBeforeClose(IWebBrowser browserControl, IBrowser browser)
        {
        }
    }
}
