#pragma once
class FFmpegTest :
	public CWinApp
{
public:
	FFmpegTest();
	~FFmpegTest();

	// ��д
public:
	virtual BOOL InitInstance();
	DECLARE_MESSAGE_MAP();

};

extern FFmpegTest theTest;

