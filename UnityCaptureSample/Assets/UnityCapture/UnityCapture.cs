/*
  Unity Capture
  Copyright (c) 2018 Bernhard Schelling

  Based on UnityCam
  https://github.com/mrayy/UnityCam
  Copyright (c) 2016 MHD Yamen Saraiji

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/

using UnityEngine;

[RequireComponent(typeof(Camera))]
public class UnityCapture : MonoBehaviour
{
    const string DllName = "UnityCapturePlugin";
    enum ECaptureSendResult { SUCCESS = 0, WARNING_FRAMESKIP = 1, WARNING_CAPTUREINACTIVE = 2, ERROR_UNSUPPORTEDGRAPHICSDEVICE = 100, ERROR_PARAMETER = 101, ERROR_TOOLARGERESOLUTION = 102, ERROR_TEXTUREFORMAT = 103, ERROR_READTEXTURE = 104 };
    enum EResizeMode { DisabledShowMessage = 0, LinearResize = 1 }
    enum EMirrorMode { Disabled = 0, MirrorHorizontally = 1 }
    [System.Runtime.InteropServices.DllImport(DllName)] extern static System.IntPtr CaptureCreateInstance();
    [System.Runtime.InteropServices.DllImport(DllName)] extern static void CaptureDeleteInstance(System.IntPtr instance);
    [System.Runtime.InteropServices.DllImport(DllName)] extern static ECaptureSendResult CaptureSendTexture(System.IntPtr instance, System.IntPtr nativetexture, bool UseDoubleBuffering, EResizeMode ResizeMode, EMirrorMode MirrorMode);
    System.IntPtr CaptureInstance;

    [SerializeField] [Tooltip("Scale image if Unity and capture resolution don't match (can introduce frame dropping, not recommended)")] EResizeMode ResizeMode = EResizeMode.DisabledShowMessage;
    [SerializeField] [Tooltip("Mirror captured output image")] EMirrorMode MirrorMode = EMirrorMode.Disabled;
    [SerializeField] [Tooltip("Introduce a frame of latency in favor of frame rate")] bool DoubleBuffering = false;
    [SerializeField] [Tooltip("Check to enable VSync during capturing")] bool EnableVSync = false;
    [SerializeField] [Tooltip("Set the desired render target frame rate")] int TargetFrameRate = 60;

    void Awake()
    {
        QualitySettings.vSyncCount = (EnableVSync ? 1 : 0);
        Application.targetFrameRate = TargetFrameRate;

        if (Application.runInBackground == false)
        {
            Debug.LogWarning("Application.runInBackground switched to enabled for capture streaming");
            Application.runInBackground = true;
        }
    }

    void Start()
    {
        CaptureInstance = CaptureCreateInstance();
    }

    void OnDestroy()
    {
        CaptureDeleteInstance(CaptureInstance);
    }

    void OnRenderImage(RenderTexture source, RenderTexture destination)
    {
        Graphics.Blit(source, destination);
        switch (CaptureSendTexture(CaptureInstance, source.GetNativeTexturePtr(), DoubleBuffering, ResizeMode, MirrorMode))
        {
            case ECaptureSendResult.SUCCESS: break;
            case ECaptureSendResult.WARNING_FRAMESKIP:               Debug.LogWarning("[UnityCapture] Capture device did skip a frame read, capture frame rate will not match render frame rate."); break;
            case ECaptureSendResult.WARNING_CAPTUREINACTIVE:         Debug.LogWarning("[UnityCapture] Capture device is inactive"); break;
            case ECaptureSendResult.ERROR_UNSUPPORTEDGRAPHICSDEVICE: Debug.LogError("[UnityCapture] Unsupported graphics device (only D3D11 supported)"); break;
            case ECaptureSendResult.ERROR_PARAMETER:                 Debug.LogError("[UnityCapture] Input parameter error"); break;
            case ECaptureSendResult.ERROR_TOOLARGERESOLUTION:        Debug.LogError("[UnityCapture] Render resolution is too large to send to capture device"); break;
            case ECaptureSendResult.ERROR_TEXTUREFORMAT:             Debug.LogError("[UnityCapture] Render texture format is unsupported (make sure the main camera has 'Allow HDR' set to off)"); break;
            case ECaptureSendResult.ERROR_READTEXTURE:               Debug.LogError("[UnityCapture] Error while reading texture image data"); break;
        }
    }
}
