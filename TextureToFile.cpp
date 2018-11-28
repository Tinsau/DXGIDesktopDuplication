// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) IQIYI Corporation. All rights reserved

#include "TextureToFile.h"

#include <wrl.h>
#include <wincodec.h>
#include <exception>
#include <sstream>
#include <queue>

using namespace Microsoft::WRL;

PCSTR StringFromWicFormat(const GUID& FormatGuid)
{
	if (FormatGuid == GUID_WICPixelFormat32bppRGBA) {
		return "GUID_WICPixelFormat32bppRGBA";
	}
	else if (FormatGuid == GUID_WICPixelFormat32bppBGRA) {
		return "GUID_WICPixelFormat32bppBGRA";
	}

	return "Unknown pixel format";
}

class AutoTextureMap
{
public:
	AutoTextureMap(ID3D11Texture2D* Texture)
		: mTexture(Texture)
	{
		// Get the device context
		Texture->GetDevice(&mD3dDevice);
		mD3dDevice->GetImmediateContext(&mD3dContext);
		// map the texture
		mMapInfo.RowPitch;
		HRESULT hr = mD3dContext->Map(
			Texture,
			0,  // Subresource
			D3D11_MAP_READ,
			0,  // MapFlags
			&mMapInfo);
		if (FAILED(hr)) {
			mMapped = false;
		}
		else {
			mMapped = true;
		}
	}
	~AutoTextureMap()
	{
		if (mMapped) {
			mD3dContext->Unmap(mTexture, 0);
		}
	}

	bool mMapped;
	ID3D11Texture2D* mTexture;
	D3D11_MAPPED_SUBRESOURCE mMapInfo;
	ComPtr<ID3D11Device> mD3dDevice;
	ComPtr<ID3D11DeviceContext> mD3dContext;
};

void D3D11CopyTexture(ID3D11Texture2D** DestTexture, ID3D11Texture2D* SrcSurface, ID3D11Device * device, ID3D11DeviceContext * deviceContext)
{
	D3D11_TEXTURE2D_DESC SrcDesc;
	SrcSurface->GetDesc(&SrcDesc);

	// Staging buffer/texture
	D3D11_TEXTURE2D_DESC DestBufferDesc;
	DestBufferDesc.Width = SrcDesc.Width;
	DestBufferDesc.Height = SrcDesc.Height;
	DestBufferDesc.MipLevels = 1;
	DestBufferDesc.ArraySize = 1;
	DestBufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	DestBufferDesc.SampleDesc.Count = 1;
	DestBufferDesc.SampleDesc.Quality = 0;
	DestBufferDesc.Usage = D3D11_USAGE_STAGING;
	DestBufferDesc.BindFlags = 0;
	DestBufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	DestBufferDesc.MiscFlags = 0;

	*DestTexture = nullptr;

	ID3D11Texture2D* DestBuffer = nullptr;
	HRESULT hr = device->CreateTexture2D(&DestBufferDesc, nullptr, &DestBuffer);
	if (FAILED(hr)) return;
	deviceContext->CopyResource(DestBuffer, SrcSurface);

	*DestTexture = DestBuffer;
}


void SaveTextureToBmp(PCWSTR FileName, ID3D11Texture2D* Texture)
{
	HRESULT hr;

	// First verify that we can map the texture
	D3D11_TEXTURE2D_DESC desc;
	Texture->GetDesc(&desc);

	// translate texture format to WIC format. We support only BGRA and ARGB.
	GUID wicFormatGuid;
	switch (desc.Format) {
	case DXGI_FORMAT_R8G8B8A8_UNORM:
		wicFormatGuid = GUID_WICPixelFormat32bppRGBA;
		break;
	case DXGI_FORMAT_B8G8R8A8_UNORM:
		wicFormatGuid = GUID_WICPixelFormat32bppBGRA;
		break;
	default:
	{
		std::stringstream ss;
		ss << "Unsupported DXGI_FORMAT: " << desc.Format << ". Only RGBA and BGRA are supported.";
		throw std::exception(ss.str().c_str());
	}
	}

	AutoTextureMap texMap(Texture);
	if (!texMap.mMapped) {
		return;
	}
	
	ComPtr<IWICImagingFactory> wicFactory;
	hr = CoCreateInstance(
		CLSID_WICImagingFactory,
		nullptr,
		CLSCTX_INPROC_SERVER,
		__uuidof(wicFactory),
		reinterpret_cast<void**>(wicFactory.GetAddressOf()));
	if (FAILED(hr)) {
		throw std::exception("Failed to create instance of WICImagingFactory");
	}

	ComPtr<IWICBitmapEncoder> wicEncoder;
	hr = wicFactory->CreateEncoder(
		GUID_ContainerFormatBmp,
		nullptr,
		&wicEncoder);
	if (FAILED(hr)) {
		throw std::exception("Failed to create BMP encoder");
	}

	ComPtr<IWICStream> wicStream;
	hr = wicFactory->CreateStream(&wicStream);
	if (FAILED(hr)) {
		throw std::exception("Failed to create IWICStream");
	}

	hr = wicStream->InitializeFromFilename(FileName, GENERIC_WRITE);
	if (FAILED(hr)) {
		throw std::exception("Failed to initialize stream from file name");
	}

	hr = wicEncoder->Initialize(wicStream.Get(), WICBitmapEncoderNoCache);
	if (FAILED(hr)) {
		throw std::exception("Failed to initialize bitmap encoder");
	}

	// Encode and commit the frame
	{
		ComPtr<IWICBitmapFrameEncode> frameEncode;
		wicEncoder->CreateNewFrame(&frameEncode, nullptr);
		if (FAILED(hr)) {
			throw std::exception("Failed to create IWICBitmapFrameEncode");
		}

		hr = frameEncode->Initialize(nullptr);
		if (FAILED(hr)) {
			throw std::exception("Failed to initialize IWICBitmapFrameEncode");
		}


		hr = frameEncode->SetPixelFormat(&wicFormatGuid);
		if (FAILED(hr)) {
			std::stringstream ss;
			ss << "SetPixelFormat(" << StringFromWicFormat(wicFormatGuid) << "%s) failed.";
			throw std::exception(ss.str().c_str());
		}

		hr = frameEncode->SetSize(desc.Width, desc.Height);
		if (FAILED(hr)) {
			throw std::exception("SetSize(...) failed.");
		}

		hr = frameEncode->WritePixels(
			desc.Height,
			texMap.mMapInfo.RowPitch,
			desc.Height * texMap.mMapInfo.RowPitch,
			reinterpret_cast<BYTE*>(texMap.mMapInfo.pData));
		if (FAILED(hr)) {
			throw std::exception("frameEncode->WritePixels(...) failed.");
		}

		hr = frameEncode->Commit();
		if (FAILED(hr)) {
			throw std::exception("Failed to commit frameEncode");
		}
	}

	hr = wicEncoder->Commit();
	if (FAILED(hr)) {
		throw std::exception("Failed to commit encoder");
	}
}

// ansyc save tex to bitmap
struct Texture2DData
{
	Texture2DData() {
		Inited = false;
		pData = nullptr;
	}
	~Texture2DData() {
		if (Inited && pData) {
			free(pData);
		}
	}
	WCHAR FilePath[MAX_PATH];
	UINT Width;
	UINT Height;
	DXGI_FORMAT Format;
	void *pData;
	UINT RowPitch;
	UINT DepthPitch;
	bool Inited;
};

std::queue<Texture2DData *> gTexDatas;
HANDLE gQueueLock;
HANDLE gTerminateEvent;
HANDLE gTexDataToFileThread;
DWORD gTexDataToFileThreadId;

void AsyncSaveTextureToBmp(PCWSTR FileName, ID3D11Texture2D* Texture)
{
	// First verify that we can map the texture
	D3D11_TEXTURE2D_DESC desc;
	Texture->GetDesc(&desc);

	// translate texture format to WIC format. We support only BGRA and ARGB.
	GUID wicFormatGuid;
	switch (desc.Format) {
	case DXGI_FORMAT_R8G8B8A8_UNORM:
		wicFormatGuid = GUID_WICPixelFormat32bppRGBA;
		break;
	case DXGI_FORMAT_B8G8R8A8_UNORM:
		wicFormatGuid = GUID_WICPixelFormat32bppBGRA;
		break;
	default:
	{
		std::stringstream ss;
		ss << "Unsupported DXGI_FORMAT: " << desc.Format << ". Only RGBA and BGRA are supported.";
		throw std::exception(ss.str().c_str());
	}
	}

	AutoTextureMap texMap(Texture);
	if (!texMap.mMapped) {
		return;
	}

	Texture2DData *texData = new Texture2DData;
	if (texData == nullptr) {
		return;
	}
	UINT bufferSize = desc.Height * texMap.mMapInfo.RowPitch;
	texData->pData = malloc(bufferSize);
	if (texData->pData == nullptr) {
		delete texData;
		return;
	}
	texData->Inited = false;
	texData->Height = desc.Height;
	texData->Width = desc.Width;
	texData->Format = desc.Format;
	texData->RowPitch = texMap.mMapInfo.RowPitch;
	texData->DepthPitch = texMap.mMapInfo.DepthPitch;
	wcscpy(texData->FilePath, FileName);

	memcpy(texData->pData, texMap.mMapInfo.pData, bufferSize);

	texData->Inited = true;
	{
		::WaitForSingleObject(gQueueLock, INFINITE);
		gTexDatas.push(texData);
		::ReleaseMutex(gQueueLock);
	}
}

void SaveTexDataToBmp(Texture2DData* texData)
{
	HRESULT hr;
	PCWSTR FileName = texData->FilePath;
	// translate texture format to WIC format. We support only BGRA and ARGB.
	GUID wicFormatGuid;
	switch (texData->Format) {
	case DXGI_FORMAT_R8G8B8A8_UNORM:
		wicFormatGuid = GUID_WICPixelFormat32bppRGBA;
		break;
	case DXGI_FORMAT_B8G8R8A8_UNORM:
		wicFormatGuid = GUID_WICPixelFormat32bppBGRA;
		break;
	default:
	{
		std::stringstream ss;
		ss << "Unsupported DXGI_FORMAT: " << texData->Format << ". Only RGBA and BGRA are supported.";
		throw std::exception(ss.str().c_str());
	}
	}

	ComPtr<IWICImagingFactory> wicFactory;
	hr = CoCreateInstance(
		CLSID_WICImagingFactory,
		nullptr,
		CLSCTX_INPROC_SERVER,
		__uuidof(wicFactory),
		reinterpret_cast<void**>(wicFactory.GetAddressOf()));
	if (FAILED(hr)) {
		throw std::exception("Failed to create instance of WICImagingFactory");
	}

	ComPtr<IWICBitmapEncoder> wicEncoder;
	hr = wicFactory->CreateEncoder(
		GUID_ContainerFormatBmp,
		nullptr,
		&wicEncoder);
	if (FAILED(hr)) {
		throw std::exception("Failed to create BMP encoder");
	}

	ComPtr<IWICStream> wicStream;
	hr = wicFactory->CreateStream(&wicStream);
	if (FAILED(hr)) {
		throw std::exception("Failed to create IWICStream");
	}

	hr = wicStream->InitializeFromFilename(FileName, GENERIC_WRITE);
	if (FAILED(hr)) {
		throw std::exception("Failed to initialize stream from file name");
	}

	hr = wicEncoder->Initialize(wicStream.Get(), WICBitmapEncoderNoCache);
	if (FAILED(hr)) {
		throw std::exception("Failed to initialize bitmap encoder");
	}

	// Encode and commit the frame
	{
		ComPtr<IWICBitmapFrameEncode> frameEncode;
		wicEncoder->CreateNewFrame(&frameEncode, nullptr);
		if (FAILED(hr)) {
			throw std::exception("Failed to create IWICBitmapFrameEncode");
		}

		hr = frameEncode->Initialize(nullptr);
		if (FAILED(hr)) {
			throw std::exception("Failed to initialize IWICBitmapFrameEncode");
		}


		hr = frameEncode->SetPixelFormat(&wicFormatGuid);
		if (FAILED(hr)) {
			std::stringstream ss;
			ss << "SetPixelFormat(" << StringFromWicFormat(wicFormatGuid) << "%s) failed.";
			throw std::exception(ss.str().c_str());
		}

		hr = frameEncode->SetSize(texData->Width, texData->Height);
		if (FAILED(hr)) {
			throw std::exception("SetSize(...) failed.");
		}

		hr = frameEncode->WritePixels(
			texData->Height,
			texData->RowPitch,
			texData->Height * texData->RowPitch,
			reinterpret_cast<BYTE*>(texData->pData));
		if (FAILED(hr)) {
			throw std::exception("frameEncode->WritePixels(...) failed.");
		}

		hr = frameEncode->Commit();
		if (FAILED(hr)) {
			throw std::exception("Failed to commit frameEncode");
		}
	}

	hr = wicEncoder->Commit();
	if (FAILED(hr)) {
		throw std::exception("Failed to commit encoder");
	}
}


// texture output thread
DWORD WINAPI TexDataToFileThreadFunction(LPVOID lpParam)
{
	while (true) {
		DWORD waitRet = ::WaitForSingleObject(gTerminateEvent, 16);
		if (waitRet == WAIT_OBJECT_0) {
			return 0;
		}
		else if (waitRet == WAIT_TIMEOUT) {

			Texture2DData *texData = nullptr;
			{
				::WaitForSingleObject(gQueueLock, INFINITE);
				if (gTexDatas.size() > 0) {
					texData = gTexDatas.front();
					gTexDatas.pop();
				}
				::ReleaseMutex(gQueueLock);
			}
			if (texData) {

				SaveTexDataToBmp(texData);
				delete texData;
			}
		}
	}

	return 0;
}
void StopAnsycSaveTextureThread()
{
	SetEvent(gTerminateEvent);
	WaitForSingleObject(gTexDataToFileThread, INFINITE);
	CloseHandle(gTexDataToFileThread);
	CloseHandle(gTerminateEvent);
	CloseHandle(gQueueLock);
}
void StartAnsycSaveTextureThread()
{
	gQueueLock = CreateMutex(NULL, FALSE, NULL);
	gTerminateEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	gTexDataToFileThread = CreateThread(
		NULL,                   // default security attributes
		0,                      // use default stack size  
		TexDataToFileThreadFunction,       // thread function name
		0,          // argument to thread function 
		0,                      // use default creation flags 
		&gTexDataToFileThreadId);   // returns the thread identifier 
}


std::wstring ExePath() {
	wchar_t buffer[MAX_PATH];
	GetModuleFileName(NULL, buffer, MAX_PATH);
	std::wstring::size_type pos = std::wstring(buffer).find_last_of(L"\\/");
	return std::wstring(buffer).substr(0, pos);
}

BOOL DirectoryExists(LPCTSTR szPath)
{
	DWORD dwAttrib = GetFileAttributes(szPath);

	return (dwAttrib != INVALID_FILE_ATTRIBUTES &&
		(dwAttrib & FILE_ATTRIBUTE_DIRECTORY));
}