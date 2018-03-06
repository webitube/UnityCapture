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

#include "shared.inl"
#include <chrono>
#include <string>
#include "IUnityGraphics.h"

enum
{
	RET_SUCCESS = 0,
	RET_WARNING_FRAMESKIP = 1,
	RET_WARNING_CAPTUREINACTIVE = 2,
	RET_ERROR_UNSUPPORTEDGRAPHICSDEVICE = 100,
	RET_ERROR_PARAMETER = 101,
	RET_ERROR_TOOLARGERESOLUTION = 102,
	RET_ERROR_TEXTUREFORMAT = 103,
	RET_ERROR_READTEXTURE = 104,
};

enum EMirrorMode { MIRRORMODE_DISABLED = 0, MIRRORMODE_HORIZONTALLY = 1 };

#include <d3d11.h>

static int g_GraphicsDeviceType = -1;
static ID3D11Device* g_D3D11GraphicsDevice = 0;

struct UnityCaptureInstance
{
	SharedImageMemory* m_sender;
	int m_width;
	int m_height;
	ID3D11Texture2D* m_textureBuf;
};

extern "C" __declspec(dllexport) UnityCaptureInstance* CaptureCreateInstance()
{
	UnityCaptureInstance* c = new UnityCaptureInstance();
	c->m_sender = new SharedImageMemory();
	c->m_width = 0;
	c->m_height = 0;
	c->m_textureBuf = 0;
	return c;
}

extern "C" __declspec(dllexport) void CaptureDeleteInstance(UnityCaptureInstance* c)
{
	if (!c) return;
	delete c->m_sender;
	if (c->m_textureBuf) c->m_textureBuf->Release();
	delete c;
}

extern "C" __declspec(dllexport) int CaptureSendTexture(UnityCaptureInstance* c, void* TextureNativePtr, SharedImageMemory::EResizeMode ResizeMode, EMirrorMode MirrorMode)
{
	if (!c || !TextureNativePtr) return RET_ERROR_PARAMETER;
	if (g_GraphicsDeviceType != kUnityGfxRendererD3D11) return RET_ERROR_UNSUPPORTEDGRAPHICSDEVICE;

	//Get the active D3D11 context
	ID3D11DeviceContext* ctx = NULL;
	g_D3D11GraphicsDevice->GetImmediateContext(&ctx);
	if (!ctx) return RET_ERROR_UNSUPPORTEDGRAPHICSDEVICE;

	//Read the size and format info from the render texture
	ID3D11Texture2D* d3dtex = (ID3D11Texture2D*)TextureNativePtr;
	D3D11_TEXTURE2D_DESC desc = {0};
	d3dtex->GetDesc(&desc);
	if (!desc.Width || !desc.Height) return RET_ERROR_READTEXTURE;

	if (c->m_width != desc.Width || c->m_height != desc.Height)
	{
		//Allocate a Texture2D resource which holds the texture with CPU memory access
		D3D11_TEXTURE2D_DESC textureDesc;
		ZeroMemory(&textureDesc, sizeof(textureDesc));
		textureDesc.Width = desc.Width;
		textureDesc.Height = desc.Height;
		textureDesc.MipLevels = 1;
		textureDesc.ArraySize = 1;
		textureDesc.Format = desc.Format;
		textureDesc.SampleDesc.Count = 1;
		textureDesc.SampleDesc.Quality = 0;
		textureDesc.Usage = D3D11_USAGE_STAGING;
		textureDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		textureDesc.MiscFlags = 0;
		if (c->m_textureBuf) c->m_textureBuf->Release();
		g_D3D11GraphicsDevice->CreateTexture2D(&textureDesc, NULL, &c->m_textureBuf);
		c->m_width = desc.Width;
		c->m_height = desc.Height;
	}

	//Check texture format
	int RGBABits = 0;
	if (desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM || desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || desc.Format == DXGI_FORMAT_R8G8B8A8_UINT || desc.Format == DXGI_FORMAT_R8G8B8A8_TYPELESS) RGBABits = 8;
	//if (desc.Format == DXGI_FORMAT_R16G16B16A16_UNORM || desc.Format == DXGI_FORMAT_R16G16B16A16_UINT || desc.Format == DXGI_FORMAT_R16G16B16A16_TYPELESS) RGBABits = 16; //not supported for now
	if (!RGBABits) return RET_ERROR_TEXTUREFORMAT;

	//Copy render texture to texture with CPU access and map the image data to RAM
	ctx->CopyResource(c->m_textureBuf, d3dtex);
	D3D11_MAPPED_SUBRESOURCE mapResource;
	if (FAILED(ctx->Map(c->m_textureBuf, 0, D3D11_MAP_READ, NULL, &mapResource))) return RET_ERROR_READTEXTURE;

	//Read image block without row gaps (where pitch is larger than width), also change from RGBA to BGR while at it
	//We write the modified image back into the same memory because it always fits (3 instead of 4 pixels, guaranteed to have no gap at the row end)
	//so there is no need for a temporary buffer to store the modified result before sending.
	if (RGBABits == 8)
	{
		const unsigned width = desc.Width, height = desc.Height, dstPitch = width * 3, srcPitch = mapResource.RowPitch / 4;
		unsigned char *dst = (unsigned char*)mapResource.pData, *dstEnd = dst + width * height * 3, *dstRowEnd;
		uint32_t* src = (uint32_t*)mapResource.pData, *pxlSrc;
		if (MirrorMode == MIRRORMODE_HORIZONTALLY)
		{
			//Handle horizontal flipping, it is a bit slower than without flipping
			for (; dst != dstEnd; src += srcPitch)
				for (dstRowEnd = dst + dstPitch, pxlSrc = src + width - 1; dst != dstRowEnd; dst += 3, pxlSrc--)
					*(uint32_t*)dst = _byteswap_ulong(*pxlSrc) >> 8;
		}
		else if (srcPitch != width)
		{
			//Handle a case where the texture pitch does have a gap on the right side
			for (; dst != dstEnd; dst += dstPitch, src += srcPitch)
				for (unsigned i = 0; i != width; i++)
					*(uint32_t*)(dst + i * 3) = _byteswap_ulong(src[i]) >> 8;
		}
		else
		{
			//The fastest (implemented) way to convert from RGBA to BGR
			uint32_t *srcEnd8 = src + ((width*height)&~7), *srcEnd1 = src + ((width*height));
			for (; src != srcEnd8; dst += 24, src += 8)
			{
				*(uint32_t*)(dst     ) = _byteswap_ulong(src[0]) >> 8;
				*(uint32_t*)(dst +  3) = _byteswap_ulong(src[1]) >> 8;
				*(uint32_t*)(dst +  6) = _byteswap_ulong(src[2]) >> 8;
				*(uint32_t*)(dst +  9) = _byteswap_ulong(src[3]) >> 8;
				*(uint32_t*)(dst + 12) = _byteswap_ulong(src[4]) >> 8;
				*(uint32_t*)(dst + 15) = _byteswap_ulong(src[5]) >> 8;
				*(uint32_t*)(dst + 18) = _byteswap_ulong(src[6]) >> 8;
				*(uint32_t*)(dst + 21) = _byteswap_ulong(src[7]) >> 8;
			}
			for (; src != srcEnd1; dst += 3, src++)
				*(uint32_t*)(dst) = _byteswap_ulong(*src) >> 8;
		}
	}
	//else if (RGBABits == 16) //16 bit color downscaling (HDR to RGB) is not complete 
	//{
	//	unsigned char *dst = (unsigned char*)mapResource.pData; unsigned short *src = (unsigned short*)mapResource.pData;
	//	for (int row = 0, rowEnd = desc.Height, srcgap = (mapResource.RowPitch - desc.Width * 8) / 2; row < rowEnd; row++, src += srcgap)
	//		for (int i = 0; i < desc.Width; ++i, dst += 3, src += 4)
	//			dst[0] = (unsigned char)(src[0]>>4), dst[1] = (unsigned char)(src[1]>>4), dst[2] = (unsigned char)(src[2]>>4);
	//}

	//Push the captured data to the direct show filter
	SharedImageMemory::ESendResult res = c->m_sender->Send(desc.Width, desc.Height, ResizeMode, (const unsigned char*)mapResource.pData);
	ctx->Unmap(c->m_textureBuf, 0);

	switch (res)
	{
		case SharedImageMemory::SENDRES_CAPTUREINACTIVE: return RET_WARNING_CAPTUREINACTIVE;
		case SharedImageMemory::SENDRES_TOOLARGE:        return RET_ERROR_TOOLARGERESOLUTION;
		case SharedImageMemory::SENDRES_WARN_FRAMESKIP:  return RET_WARNING_FRAMESKIP;
	}
	return RET_SUCCESS;
}

// If exported by a plugin, this function will be called when graphics device is created, destroyed, and before and after it is reset (ie, resolution changed).
extern "C" void UNITY_INTERFACE_EXPORT UnitySetGraphicsDevice(void* device, int deviceType, int eventType)
{
	if (eventType == kUnityGfxDeviceEventInitialize || eventType == kUnityGfxDeviceEventAfterReset)
	{
		g_GraphicsDeviceType = deviceType;
		if (deviceType == kUnityGfxRendererD3D11) g_D3D11GraphicsDevice = (ID3D11Device*)device;
	}
	else g_GraphicsDeviceType = -1;
}
