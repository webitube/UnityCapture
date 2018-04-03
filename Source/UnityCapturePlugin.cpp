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
	DXGI_FORMAT m_format;
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

	if (c->m_width != desc.Width || c->m_height != desc.Height || c->m_format != desc.Format)
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
		c->m_format = desc.Format;
	}

	//Check texture format
	int RGBABits = 0;
	if (desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM || desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || desc.Format == DXGI_FORMAT_R8G8B8A8_UINT || desc.Format == DXGI_FORMAT_R8G8B8A8_TYPELESS) RGBABits = 8;
	if (desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT || desc.Format == DXGI_FORMAT_R16G16B16A16_TYPELESS) RGBABits = 16;
	if (!RGBABits) return RET_ERROR_TEXTUREFORMAT;

	//Copy render texture to texture with CPU access and map the image data to RAM
	ctx->CopyResource(c->m_textureBuf, d3dtex);
	D3D11_MAPPED_SUBRESOURCE mapResource;
	if (FAILED(ctx->Map(c->m_textureBuf, 0, D3D11_MAP_READ, NULL, &mapResource))) return RET_ERROR_READTEXTURE;

	//Read image block without row gaps (where pitch is larger than width), also change from RGBA to BGR while at it
	//We write the modified image back into the same memory because it always fits (3 instead of 4 pixels, guaranteed to have no gap at the row end)
	//so there is no need for a temporary buffer to store the modified result before sending.
	const unsigned width = desc.Width, height = desc.Height, dstPitch = width * 3, srcPitch = mapResource.RowPitch / (RGBABits / 2);
	uint8_t *dst = (uint8_t*)mapResource.pData, *dstEnd = dst + height * dstPitch;
	if (RGBABits == 8)
	{
		uint32_t *src = (uint32_t*)mapResource.pData;
		if (srcPitch != width)
		{
			//Handle a case where the texture pitch does have a gap on the right side
			for (; dst != dstEnd; dst += dstPitch, src += srcPitch)
				for (unsigned i = 0; i != width; i++)
					*(uint32_t*)(dst + i * 3) = _byteswap_ulong(src[i]) >> 8;
		}
		else
		{
			//The fastest (implemented) way to convert from RGBA to BGR
			uint32_t *srcEnd8 = src + ((width*height)&~7), *srcEnd1 = src + (width*height);
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
	else if (RGBABits == 16)
	{
		//16 bit color downscaling (HDR (16 bit floats) to BGR)
		float tmpf; uint16_t *tmpp, tmpr, tmpg, tmpb;
		#define F16toU8(s) ((s) & 0x8000 ? 0 : (*(uint32_t*)&tmpf = ((s) << 13) + 0x38000000, tmpf < 1.0f ? (int)(tmpf * 255.99f) : 255))
		#define RGBAF16toRGBU8(psrc) (tmpp = (uint16_t*)(psrc), tmpr = tmpp[0], tmpg = tmpp[1], tmpb = tmpp[2], (F16toU8(tmpr) << 16) | (F16toU8(tmpg) << 8) | (F16toU8(tmpb)))
		uint64_t *src = (uint64_t*)mapResource.pData;
		if (srcPitch != width)
		{
			//Handle a case where the texture pitch does have a gap on the right side
			for (; dst != dstEnd; dst += dstPitch, src += srcPitch)
				for (unsigned i = 0; i != width; i++)
					*(uint32_t*)(dst + i * 3) = RGBAF16toRGBU8(src + i);
		}
		else
		{
			//The fastest (implemented) way to convert from RGBA to BGR
			uint64_t *srcEnd8 = src + ((width*height)&~7), *srcEnd1 = src + (width*height);
			for (; src != srcEnd8; dst += 24, src += 8)
			{
				*(uint32_t*)(dst     ) = RGBAF16toRGBU8(src    );
				*(uint32_t*)(dst +  3) = RGBAF16toRGBU8(src + 1);
				*(uint32_t*)(dst +  6) = RGBAF16toRGBU8(src + 2);
				*(uint32_t*)(dst +  9) = RGBAF16toRGBU8(src + 3);
				*(uint32_t*)(dst + 12) = RGBAF16toRGBU8(src + 4);
				*(uint32_t*)(dst + 15) = RGBAF16toRGBU8(src + 5);
				*(uint32_t*)(dst + 18) = RGBAF16toRGBU8(src + 6);
				*(uint32_t*)(dst + 21) = RGBAF16toRGBU8(src + 7);
			}
			for (; src != srcEnd1; dst += 3, src++)
				*(uint32_t*)(dst) = RGBAF16toRGBU8(src);
		}
	}

	if (MirrorMode == MIRRORMODE_HORIZONTALLY)
	{
		//Handle horizontal flipping
		for (dst = (uint8_t*)mapResource.pData; dst != dstEnd; dst += width * 3)
			for (uint8_t tmp[3], *dstA = dst, *dstB = dst + width * 3 - 3; dstA < dstB; dstA += 3, dstB -= 3)
				memcpy(tmp, dstA, 3), memcpy(dstA, dstB, 3), memcpy(dstB, tmp, 3);
	}

	//Push the captured data to the direct show filter
	SharedImageMemory::ESendResult res = c->m_sender->Send(width, height, ResizeMode, (const unsigned char*)mapResource.pData);
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
