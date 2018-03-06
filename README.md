# Unity Capture

![Unity Capture and OBS](https://raw.githubusercontent.com/schellingb/UnityCapture/master/README.png)

## Overview

Unity Capture is a Windows DirectShow Filter that allows you to stream a rendered camera directly to another application.  
In more simple terms, it essentially makes Unity simulate a web cam device on Windows that shows a rendered camera.

This project is based on [UnityCam by Yamen Saraiji](//github.com/mrayy/UnityCam) with added features and big performance
improvements. It supports lag-free 1080p at 60 FPS on moderate PCs and can handle 4K resolutions on a faster PC.


## Installation

First download this project from GitHub with the 'Download ZIP' button or by cloning the repository.

To register the DirectShow Filter to be available in Windows programs, run the Install.bat inside the "Install" directory.  
Make sure the files in the install directory are placed where you want them to be.  
If you want to move or delete the files, run "Uninstall.bat" first.

If you have problems registering or unregistering, right click on the Install.bat and choose "Run as Administrator".


## Test in Unity

Open the included UnityCaptureSample project in Unity, load the scene 'UnityCaptureExample' and hit play.  
Then run a receiving application (like [OBS](https://obsproject.com/), any program with web cam support
or a [WebRTC website](https://webrtc.github.io/samples/src/content/getusermedia/resolution/)) and request
video from the "Unity Video Capture" device.

You should see the rendering output from Unity displayed in your target application.

If you see a message about matching rendering and display resolutions, use the resolution settings on
the 'Game' tab in Unity to set a fixed resolution to match the capture output.


## Setup in your Unity project

Just copy the [UnityCapture asset directory from the included sample project](UnityCaptureSample/Assets/UnityCapture)
into your own project and then add the 'Unity Capture' behavior to your camera at the bottom.

The camera setting 'Allow HDR' needs to be disabled for this to work.  
You can also enable this behavior on a secondary camera that is not your main game camera by setting a target texture
with your desired capture output resolution.

### Settings

There are two settings for the 'Unity Capture' behavior.

- 'Resize Mode': It is suggested to leave this disabled and just let your capture target application handle the display
  sizing/resizing because this setting can introduce frame skipping. So far only a very basic linear resize is supported.
- 'Mirror Mode': This setting should also be handled by your target application if possible and needed, but it is available.

### Possible errors/warnings

- Warning: "Capture device did skip a frame read, capture frame rate will not match render frame rate."  
  Output when a frame rendered by Unity was never displayed in the target capture application due
  to performance problems or target application being slow.
- Warning: "Capture device is inactive"  
  If the target capture application has not been started yet.
- Error: "Unsupported graphics device (only D3D11 supported)"  
  When Unity uses a rendering back-end other than Direct 3D 11.
- Error: "Render resolution is too large to send to capture device"  
  When trying to send data with a resolution higher than the maximum supported 3840 x 2160
- Error: "Render texture format is unsupported (make sure the main camera has 'Allow HDR' set to off)"  
  When the rendered data/color format would require additional conversation (like HDR).
- Error: "Error while reading texture image data"  
  Generic error when the plugin is unable to access the rendered image pixel data.


## Output Device Configuration

There are three settings in the configuration panel offered by the capture device. Some applications like OBS allow you to access
these settings with a 'Configure Video' button, other applications like web browsers don't.

These settings control what will be displayed in the output in case of an error:
- 'Resolution mismatch': When resizing in the Unity behavior is disabled and the capture output and the rendering resolutions don't match.
- 'Unity never started': When rendering in Unity with a 'Unity Capture' behavior has never started sending video data.
- 'Unity sending stopped': When video data stops being received (i.e. Unity game stopped or crashed).

There are four modes that can be set for the settings above:
- 'Fill Black': Show just black.
- 'Blue/Pink Pattern': A pattern that is blue and pink.
- 'Green/Yellow Pattern': A pattern that is green and yellow.
- 'Green Key (RGB #00FE00)': Fills the output with a specific color (red and blue at 0, green at 254).
  This can be used for instance in OBS with the 'Color Key' video filter to show a layer behind the video capture.
  You can use this if you want to show a 'Please stand by...' image layer while Unity stopped.

For the two colored patterns an additional text message will be displayed detailing the error.


## Todo

- Saving of the output device configuration
- Support capturing multiple cameras separately
- Bilinear resizing


## License

Unity Capture is divided into two parts that are separately licensed.  
The filter 'UnityCaptureFilter' is available under the [MIT license](https://choosealicense.com/licenses/mit/).  
The Unity plugin 'UnityCapturePlugin' is available under the [zlib license](https://choosealicense.com/licenses/zlib/) (so attribution in your Unity project is optional).
