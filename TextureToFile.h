// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) IQIYI Corporation. All rights reserved

#ifndef _TEXTURETOFILE_H_
#define _TEXTURETOFILE_H_

#include <Windows.h>
#include <d3d11.h>
#include <string>

void D3D11CopyTexture(ID3D11Texture2D** DestTexture, ID3D11Texture2D* SrcSurface, ID3D11Device * device, ID3D11DeviceContext * deviceContext);

void SaveTextureToBmp(PCWSTR FileName, ID3D11Texture2D* Texture);

void StartAnsycSaveTextureThread();
void AsyncSaveTextureToBmp(PCWSTR FileName, ID3D11Texture2D* Texture);
void StopAnsycSaveTextureThread();

std::wstring ExePath();
BOOL DirectoryExists(LPCTSTR szPath);

#endif
