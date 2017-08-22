﻿// Copyright © 2010-2017 The CefSharp Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

using System;
using System.Diagnostics;
using System.Threading;

namespace CefSharp.Example
{
    public struct JsObject 
    {
        public string Value;
    }

    public class AsyncBoundObject
    {
        //We expect an exception here, so tell VS to ignore
        [DebuggerHidden]
        public void Error()
        {
            throw new Exception("This is an exception coming from C#");
        }

        //We expect an exception here, so tell VS to ignore
        [DebuggerHidden]
        public int Div(int divident, int divisor)
        {
            return divident / divisor;
        }

        public string Hello(string name)
        {
            return "Hello " + name;
        }

        public void DoSomething()
        {
            Thread.Sleep(1000);
        }

        public JsObject[] ObjectArray(string name)
        {
            return new[] 
            {
                new JsObject() { Value = "Item1" },
                new JsObject() { Value = "Item2" }
            };
        }
    }
}
