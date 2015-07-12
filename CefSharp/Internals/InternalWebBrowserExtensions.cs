using System;

namespace CefSharp.Internals
{
    public static class InternalWebBrowserExtensions
    {
        public static IDownloadHandler GetDownloadHandler(this IWebBrowserInternal webBrowser)
        {
            return webBrowser.GetHandler(w => w.DownloadHandler);
        }

        public static IGeolocationHandler GetGeolocationHandler(this IWebBrowserInternal webBrowser)
        {
            return webBrowser.GetHandler(w => w.GeolocationHandler);
        }

        public static IDragHandler GetDragHandler(this IWebBrowserInternal webBrowser)
        {
            return webBrowser.GetHandler(w => w.DragHandler);
        }

        public static IDialogHandler GetDialogHandler(this IWebBrowserInternal webBrowser)
        {
            return webBrowser.GetHandler(w => w.DialogHandler);
        }

        public static IJsDialogHandler GetJsDialogHandler(this IWebBrowserInternal webBrowser)
        {
            return webBrowser.GetHandler(w => w.JsDialogHandler);
        }

        public static IFocusHandler GetFocusHandler(this IWebBrowserInternal webBrowser)
        {
            return webBrowser.GetHandler(w => w.FocusHandler);
        }

        public static IMenuHandler GetMenuHandler(this IWebBrowserInternal webBrowser)
        {
            return webBrowser.GetHandler(w => w.MenuHandler);
        }

        public static IRequestHandler GetRequestHandler(this IWebBrowserInternal webBrowser)
        {
            return webBrowser.GetHandler(w => w.RequestHandler);
        }

        public static IKeyboardHandler GetKeyboardHandler(this IWebBrowserInternal webBrowser)
        {
            return webBrowser.GetHandler(w => w.KeyboardHandler);
        }

        public static ILifeSpanHandler GetLifeSpanHandler(this IWebBrowserInternal webBrowser)
        {
            return webBrowser.GetHandler(w => w.LifeSpanHandler);
        }

        public static IResourceHandlerFactory GetResourceHandlerFactory(this IWebBrowserInternal webBrowser)
        {
            return webBrowser.GetHandler(w => w.ResourceHandlerFactory);
        }

        public static T GetHandler<T>(this IWebBrowserInternal webBrowser, Func<IWebBrowserInternal, T> getter) where T : class
        {
            T result = null;
            if (webBrowser != null)
            {
                result = getter(webBrowser);
            }
            return result;
        }

    }
}
