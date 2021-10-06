/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <base/math.h>
#include <base/system.h>

#include <engine/console.h>
#include <engine/storage.h>

#include <engine/shared/config.h>

#if defined(CONF_VIDEORECORDER)
#include <engine/shared/video.h>
#endif

#include <game/generated/protocol.h>

#include "compression.h"
#include "demo.h"
#include "memheap.h"
#include "network.h"
#include "snapshot.h"

static const unsigned char s_aHeaderMarker[7] = {'T', 'W', 'D', 'E', 'M', 'O', 0};
static const unsigned char s_CurVersion = 6;
static const unsigned char s_OldVersion = 3;
static const unsigned char s_Sha256Version = 6;
static const unsigned char s_VersionTickCompression = 5; // demo files with this version or higher will use `CHUNKTICKFLAG_TICK_COMPRESSED`
static const int s_LengthOffset = 152;
static const int s_NumMarkersOffset = 176;

static const ColorRGBA gs_DemoPrintColor{0.7f, 0.7f, 0.7f, 1.0f};

CDemoRecorder::CDemoRecorder(class CSnapshotDelta *pSnapshotDelta, bool NoMapData)
{
	m_File = 0;
	m_aCurrentFilename[0] = '\0';
	m_pfnFilter = 0;
	m_pUser = 0;
	m_LastTickMarker = -1;
	m_pSnapshotDelta = pSnapshotDelta;
	m_NoMapData = NoMapData;
}

// Record
int CDemoRecorder::Start(class IStorage *pStorage, class IConsole *pConsole, const char *pFilename, const char *pNetVersion, const char *pMap, SHA256_DIGEST *pSha256, unsigned Crc, const char *pType, unsigned int MapSize, unsigned char *pMapData, IOHANDLE MapFile, DEMOFUNC_FILTER pfnFilter, void *pUser)
{
	m_pfnFilter = pfnFilter;
	m_pUser = pUser;

	m_pMapData = pMapData;
	m_pConsole = pConsole;

	IOHANDLE DemoFile = pStorage->OpenFile(pFilename, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(!DemoFile)
	{
		if(m_pConsole)
		{
			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), "Unable to open '%s' for recording", pFilename);
			m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "demo_recorder", aBuf, gs_DemoPrintColor);
		}
		return -1;
	}

	CDemoHeader Header;
	CTimelineMarkers TimelineMarkers;
	if(m_File)
	{
		io_close(DemoFile);
		return -1;
	}

	bool CloseMapFile = false;

	if(MapFile)
		io_seek(MapFile, 0, IOSEEK_START);

	char aSha256[SHA256_MAXSTRSIZE];
	if(pSha256)
		sha256_str(*pSha256, aSha256, sizeof(aSha256));

	if(!pMapData && !MapFile)
	{
		// open mapfile
		char aMapFilename[128];
		// try the downloaded maps
		if(pSha256)
		{
			str_format(aMapFilename, sizeof(aMapFilename), "downloadedmaps/%s_%s.map", pMap, aSha256);
		}
		else
		{
			str_format(aMapFilename, sizeof(aMapFilename), "downloadedmaps/%s_%08x.map", pMap, Crc);
		}
		MapFile = pStorage->OpenFile(aMapFilename, IOFLAG_READ, IStorage::TYPE_ALL);
		if(!MapFile)
		{
			// try the normal maps folder
			str_format(aMapFilename, sizeof(aMapFilename), "maps/%s.map", pMap);
			MapFile = pStorage->OpenFile(aMapFilename, IOFLAG_READ, IStorage::TYPE_ALL);
		}
		if(!MapFile)
		{
			// search for the map within subfolders
			char aBuf[512];
			str_format(aMapFilename, sizeof(aMapFilename), "%s.map", pMap);
			if(pStorage->FindFile(aMapFilename, "maps", IStorage::TYPE_ALL, aBuf, sizeof(aBuf)))
				MapFile = pStorage->OpenFile(aBuf, IOFLAG_READ, IStorage::TYPE_ALL);
		}
		if(!MapFile)
		{
			if(m_pConsole)
			{
				char aBuf[256];
				str_format(aBuf, sizeof(aBuf), "Unable to open mapfile '%s'", pMap);
				m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "demo_recorder", aBuf, gs_DemoPrintColor);
			}
			return -1;
		}

		CloseMapFile = true;
	}

	if(MapFile)
		MapSize = io_length(MapFile);

	// write header
	mem_zero(&Header, sizeof(Header));
	mem_copy(Header.m_aMarker, s_aHeaderMarker, sizeof(Header.m_aMarker));
	Header.m_Version = s_CurVersion;
	str_copy(Header.m_aNetversion, pNetVersion, sizeof(Header.m_aNetversion));
	str_copy(Header.m_aMapName, pMap, sizeof(Header.m_aMapName));
	Header.m_aMapSize[0] = (MapSize >> 24) & 0xff;
	Header.m_aMapSize[1] = (MapSize >> 16) & 0xff;
	Header.m_aMapSize[2] = (MapSize >> 8) & 0xff;
	Header.m_aMapSize[3] = (MapSize)&0xff;
	Header.m_aMapCrc[0] = (Crc >> 24) & 0xff;
	Header.m_aMapCrc[1] = (Crc >> 16) & 0xff;
	Header.m_aMapCrc[2] = (Crc >> 8) & 0xff;
	Header.m_aMapCrc[3] = (Crc)&0xff;
	str_copy(Header.m_aType, pType, sizeof(Header.m_aType));
	// Header.m_Length - add this on stop
	str_timestamp(Header.m_aTimestamp, sizeof(Header.m_aTimestamp));
	io_write(DemoFile, &Header, sizeof(Header));
	io_write(DemoFile, &TimelineMarkers, sizeof(TimelineMarkers)); // fill this on stop

	//Write Sha256
	io_write(DemoFile, SHA256_EXTENSION.m_aData, sizeof(SHA256_EXTENSION.m_aData));
	io_write(DemoFile, pSha256, sizeof(SHA256_DIGEST));

	if(m_NoMapData)
	{
	}
	else if(pMapData)
	{
		io_write(DemoFile, pMapData, MapSize);
	}
	else
	{
		// write map data
		while(1)
		{
			unsigned char aChunk[1024 * 64];
			int Bytes = io_read(MapFile, &aChunk, sizeof(aChunk));
			if(Bytes <= 0)
				break;
			io_write(DemoFile, &aChunk, Bytes);
		}
		if(CloseMapFile)
			io_close(MapFile);
		else
			io_seek(MapFile, 0, IOSEEK_START);
	}

	m_LastKeyFrame = -1;
	m_LastTickMarker = -1;
	m_FirstTick = -1;
	m_NumTimelineMarkers = 0;

	if(m_pConsole)
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "Recording to '%s'", pFilename);
		m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "demo_recorder", aBuf, gs_DemoPrintColor);
	}
	m_File = DemoFile;
	str_copy(m_aCurrentFilename, pFilename, sizeof(m_aCurrentFilename));

	return 0;
}

/*
	Tickmarker
		7	= Always set
		6	= Keyframe flag
		0-5	= Delta tick

	Normal
		7 = Not set
		5-6	= Type
		0-4	= Size
*/

enum
{
	CHUNKTYPEFLAG_TICKMARKER = 0x80,
	CHUNKTICKFLAG_KEYFRAME = 0x40, // only when tickmarker is set
	CHUNKTICKFLAG_TICK_COMPRESSED = 0x20, // when we store the tick value in the first chunk

	CHUNKMASK_TICK = 0x1f,
	CHUNKMASK_TICK_LEGACY = 0x3f,
	CHUNKMASK_TYPE = 0x60,
	CHUNKMASK_SIZE = 0x1f,

	CHUNKTYPE_SNAPSHOT = 1,
	CHUNKTYPE_MESSAGE = 2,
	CHUNKTYPE_DELTA = 3,

	CHUNKFLAG_BIGSIZE = 0x10
};

void CDemoRecorder::WriteTickMarker(int Tick, int Keyframe)
{
	if(m_LastTickMarker == -1 || Tick - m_LastTickMarker > CHUNKMASK_TICK || Keyframe)
	{
		unsigned char aChunk[5];
		aChunk[0] = CHUNKTYPEFLAG_TICKMARKER;
		aChunk[1] = (Tick >> 24) & 0xff;
		aChunk[2] = (Tick >> 16) & 0xff;
		aChunk[3] = (Tick >> 8) & 0xff;
		aChunk[4] = (Tick)&0xff;

		if(Keyframe)
			aChunk[0] |= CHUNKTICKFLAG_KEYFRAME;

		io_write(m_File, aChunk, sizeof(aChunk));
	}
	else
	{
		unsigned char aChunk[1];
		aChunk[0] = CHUNKTYPEFLAG_TICKMARKER | CHUNKTICKFLAG_TICK_COMPRESSED | (Tick - m_LastTickMarker);
		io_write(m_File, aChunk, sizeof(aChunk));
	}

	m_LastTickMarker = Tick;
	if(m_FirstTick < 0)
		m_FirstTick = Tick;
}

void CDemoRecorder::Write(int Type, const void *pData, int Size)
{
	char aBuffer[64 * 1024];
	char aBuffer2[64 * 1024];
	unsigned char aChunk[3];

	if(!m_File)
		return;

	if(Size > 64 * 1024)
		return;

	/* pad the data with 0 so we get an alignment of 4,
	else the compression won't work and miss some bytes */
	mem_copy(aBuffer2, pData, Size);
	while(Size & 3)
		aBuffer2[Size++] = 0;
	Size = CVariableInt::Compress(aBuffer2, Size, aBuffer, sizeof(aBuffer)); // buffer2 -> buffer
	if(Size < 0)
		return;

	Size = CNetBase::Compress(aBuffer, Size, aBuffer2, sizeof(aBuffer2)); // buffer -> buffer2
	if(Size < 0)
		return;

	aChunk[0] = ((Type & 0x3) << 5);
	if(Size < 30)
	{
		aChunk[0] |= Size;
		io_write(m_File, aChunk, 1);
	}
	else
	{
		if(Size < 256)
		{
			aChunk[0] |= 30;
			aChunk[1] = Size & 0xff;
			io_write(m_File, aChunk, 2);
		}
		else
		{
			aChunk[0] |= 31;
			aChunk[1] = Size & 0xff;
			aChunk[2] = Size >> 8;
			io_write(m_File, aChunk, 3);
		}
	}

	io_write(m_File, aBuffer2, Size);
}

void CDemoRecorder::RecordSnapshot(int Tick, const void *pData, int Size)
{
	if(m_LastKeyFrame == -1 || (Tick - m_LastKeyFrame) > SERVER_TICK_SPEED * 5)
	{
		// write full tickmarker
		WriteTickMarker(Tick, 1);

		// write snapshot
		Write(CHUNKTYPE_SNAPSHOT, pData, Size);

		m_LastKeyFrame = Tick;
		mem_copy(m_aLastSnapshotData, pData, Size);
	}
	else
	{
		// create delta, prepend tick
		char aDeltaData[CSnapshot::MAX_SIZE + sizeof(int)];
		int DeltaSize;

		// write tickmarker
		WriteTickMarker(Tick, 0);

		DeltaSize = m_pSnapshotDelta->CreateDelta((CSnapshot *)m_aLastSnapshotData, (CSnapshot *)pData, &aDeltaData);
		if(DeltaSize)
		{
			// record delta
			Write(CHUNKTYPE_DELTA, aDeltaData, DeltaSize);
			mem_copy(m_aLastSnapshotData, pData, Size);
		}
	}
}

void CDemoRecorder::RecordMessage(const void *pData, int Size)
{
	if(m_pfnFilter)
	{
		if(m_pfnFilter(pData, Size, m_pUser))
		{
			return;
		}
	}
	Write(CHUNKTYPE_MESSAGE, pData, Size);
}

int CDemoRecorder::Stop()
{
	if(!m_File)
		return -1;

	// add the demo length to the header
	io_seek(m_File, s_LengthOffset, IOSEEK_START);
	int DemoLength = Length();
	char aLength[4];
	aLength[0] = (DemoLength >> 24) & 0xff;
	aLength[1] = (DemoLength >> 16) & 0xff;
	aLength[2] = (DemoLength >> 8) & 0xff;
	aLength[3] = (DemoLength)&0xff;
	io_write(m_File, aLength, sizeof(aLength));

	// add the timeline markers to the header
	io_seek(m_File, s_NumMarkersOffset, IOSEEK_START);
	char aNumMarkers[4];
	aNumMarkers[0] = (m_NumTimelineMarkers >> 24) & 0xff;
	aNumMarkers[1] = (m_NumTimelineMarkers >> 16) & 0xff;
	aNumMarkers[2] = (m_NumTimelineMarkers >> 8) & 0xff;
	aNumMarkers[3] = (m_NumTimelineMarkers)&0xff;
	io_write(m_File, aNumMarkers, sizeof(aNumMarkers));
	for(int i = 0; i < m_NumTimelineMarkers; i++)
	{
		int Marker = m_aTimelineMarkers[i];
		char aMarker[4];
		aMarker[0] = (Marker >> 24) & 0xff;
		aMarker[1] = (Marker >> 16) & 0xff;
		aMarker[2] = (Marker >> 8) & 0xff;
		aMarker[3] = (Marker)&0xff;
		io_write(m_File, aMarker, sizeof(aMarker));
	}

	io_close(m_File);
	m_File = 0;
	if(m_pConsole)
		m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "demo_recorder", "Stopped recording", gs_DemoPrintColor);

	return 0;
}

void CDemoRecorder::AddDemoMarker()
{
	if(m_LastTickMarker < 0 || m_NumTimelineMarkers >= MAX_TIMELINE_MARKERS)
		return;

	// not more than 1 marker in a second
	if(m_NumTimelineMarkers > 0)
	{
		int Diff = m_LastTickMarker - m_aTimelineMarkers[m_NumTimelineMarkers - 1];
		if(Diff < SERVER_TICK_SPEED * 1.0f)
			return;
	}

	m_aTimelineMarkers[m_NumTimelineMarkers++] = m_LastTickMarker;

	if(m_pConsole)
		m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "demo_recorder", "Added timeline marker", gs_DemoPrintColor);
}

CDemoPlayer::CDemoPlayer(class CSnapshotDelta *pSnapshotDelta, TUpdateIntraTimesFunc &&UpdateIntraTimesFunc)
{
	Construct(pSnapshotDelta);

	m_UpdateIntraTimesFunc = UpdateIntraTimesFunc;
}

CDemoPlayer::CDemoPlayer(class CSnapshotDelta *pSnapshotDelta)
{
	Construct(pSnapshotDelta);
}

void CDemoPlayer::Construct(class CSnapshotDelta *pSnapshotDelta)
{
	m_File = 0;
	m_pKeyFrames = 0;
	m_SpeedIndex = 4;

	m_TickTime = 0;
	m_Time = 0;

	m_pSnapshotDelta = pSnapshotDelta;
	m_LastSnapshotDataSize = -1;
}

void CDemoPlayer::SetListener(IListener *pListener)
{
	m_pListener = pListener;
}

int CDemoPlayer::ReadChunkHeader(int *pType, int *pSize, int *pTick)
{
	unsigned char Chunk = 0;

	*pSize = 0;
	*pType = 0;

	if(m_File == NULL)
		return -1;

	if(io_read(m_File, &Chunk, sizeof(Chunk)) != sizeof(Chunk))
		return -1;

	if(Chunk & CHUNKTYPEFLAG_TICKMARKER)
	{
		// decode tick marker
		int Tickdelta_legacy = Chunk & (CHUNKMASK_TICK_LEGACY); // compatibility
		*pType = Chunk & (CHUNKTYPEFLAG_TICKMARKER | CHUNKTICKFLAG_KEYFRAME);

		if(m_Info.m_Header.m_Version < s_VersionTickCompression && Tickdelta_legacy != 0)
		{
			*pTick += Tickdelta_legacy;
		}
		else if(Chunk & (CHUNKTICKFLAG_TICK_COMPRESSED))
		{
			int Tickdelta = Chunk & (CHUNKMASK_TICK);
			*pTick += Tickdelta;
		}
		else
		{
			unsigned char aTickdata[4];
			if(io_read(m_File, aTickdata, sizeof(aTickdata)) != sizeof(aTickdata))
				return -1;
			*pTick = (aTickdata[0] << 24) | (aTickdata[1] << 16) | (aTickdata[2] << 8) | aTickdata[3];
		}
	}
	else
	{
		// decode normal chunk
		*pType = (Chunk & CHUNKMASK_TYPE) >> 5;
		*pSize = Chunk & CHUNKMASK_SIZE;

		if(*pSize == 30)
		{
			unsigned char aSizedata[1];
			if(io_read(m_File, aSizedata, sizeof(aSizedata)) != sizeof(aSizedata))
				return -1;
			*pSize = aSizedata[0];
		}
		else if(*pSize == 31)
		{
			unsigned char aSizedata[2];
			if(io_read(m_File, aSizedata, sizeof(aSizedata)) != sizeof(aSizedata))
				return -1;
			*pSize = (aSizedata[1] << 8) | aSizedata[0];
		}
	}

	return 0;
}

void CDemoPlayer::ScanFile()
{
	long StartPos;
	CHeap Heap;
	CKeyFrameSearch *pFirstKey = 0;
	CKeyFrameSearch *pCurrentKey = 0;
	//DEMOREC_CHUNK chunk;
	int ChunkSize, ChunkType, ChunkTick = 0;
	int i;

	StartPos = io_tell(m_File);
	m_Info.m_SeekablePoints = 0;

	while(1)
	{
		long CurrentPos = io_tell(m_File);

		if(ReadChunkHeader(&ChunkType, &ChunkSize, &ChunkTick))
			break;

		// read the chunk
		if(ChunkType & CHUNKTYPEFLAG_TICKMARKER)
		{
			if(ChunkType & CHUNKTICKFLAG_KEYFRAME)
			{
				CKeyFrameSearch *pKey;

				// save the position
				pKey = (CKeyFrameSearch *)Heap.Allocate(sizeof(CKeyFrameSearch));
				pKey->m_Frame.m_Filepos = CurrentPos;
				pKey->m_Frame.m_Tick = ChunkTick;
				pKey->m_pNext = 0;
				if(pCurrentKey)
					pCurrentKey->m_pNext = pKey;
				if(!pFirstKey)
					pFirstKey = pKey;
				pCurrentKey = pKey;
				m_Info.m_SeekablePoints++;
			}

			if(m_Info.m_Info.m_FirstTick == -1)
				m_Info.m_Info.m_FirstTick = ChunkTick;
			m_Info.m_Info.m_LastTick = ChunkTick;
		}
		else if(ChunkSize)
			io_skip(m_File, ChunkSize);
	}

	// copy all the frames to an array instead for fast access
	m_pKeyFrames = (CKeyFrame *)calloc(maximum(m_Info.m_SeekablePoints, 1), sizeof(CKeyFrame));
	for(pCurrentKey = pFirstKey, i = 0; pCurrentKey; pCurrentKey = pCurrentKey->m_pNext, i++)
		m_pKeyFrames[i] = pCurrentKey->m_Frame;

	// destroy the temporary heap and seek back to the start
	io_seek(m_File, StartPos, IOSEEK_START);
}

void CDemoPlayer::DoTick()
{
	static char s_aCompresseddata[CSnapshot::MAX_SIZE];
	static char s_aDecompressed[CSnapshot::MAX_SIZE];
	static char s_aData[CSnapshot::MAX_SIZE];
	int ChunkType, ChunkTick, ChunkSize;
	int DataSize = 0;
	int GotSnapshot = 0;

	// update ticks
	m_Info.m_PreviousTick = m_Info.m_Info.m_CurrentTick;
	m_Info.m_Info.m_CurrentTick = m_Info.m_NextTick;
	ChunkTick = m_Info.m_Info.m_CurrentTick;

	int64_t Freq = time_freq();
	int64_t CurtickStart = (m_Info.m_Info.m_CurrentTick) * Freq / SERVER_TICK_SPEED;
	int64_t PrevtickStart = (m_Info.m_PreviousTick) * Freq / SERVER_TICK_SPEED;
	m_Info.m_IntraTick = (m_Info.m_CurrentTime - PrevtickStart) / (float)(CurtickStart - PrevtickStart);
	m_Info.m_IntraTickSincePrev = (m_Info.m_CurrentTime - PrevtickStart) / (float)(Freq / SERVER_TICK_SPEED);
	m_Info.m_TickTime = (m_Info.m_CurrentTime - PrevtickStart) / (float)Freq;
	if(m_UpdateIntraTimesFunc)
		m_UpdateIntraTimesFunc();

	while(1)
	{
		if(ReadChunkHeader(&ChunkType, &ChunkSize, &ChunkTick))
		{
			// stop on error or eof
			if(m_pConsole)
				m_pConsole->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "demo_player", "end of file");
#if defined(CONF_VIDEORECORDER)
			if(IVideo::Current())
				Stop();
#endif
			if(m_Info.m_PreviousTick == -1)
			{
				if(m_pConsole)
					m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "demo_player", "empty demo");
				Stop();
			}
			else
				Pause();
			break;
		}

		// read the chunk
		if(ChunkSize)
		{
			if(io_read(m_File, s_aCompresseddata, ChunkSize) != (unsigned)ChunkSize)
			{
				// stop on error or eof
				if(m_pConsole)
					m_pConsole->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "demo_player", "error reading chunk");
				Stop();
				break;
			}

			DataSize = CNetBase::Decompress(s_aCompresseddata, ChunkSize, s_aDecompressed, sizeof(s_aDecompressed));
			if(DataSize < 0)
			{
				// stop on error or eof
				if(m_pConsole)
					m_pConsole->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "demo_player", "error during network decompression");
				Stop();
				break;
			}

			DataSize = CVariableInt::Decompress(s_aDecompressed, DataSize, s_aData, sizeof(s_aData));

			if(DataSize < 0)
			{
				if(m_pConsole)
					m_pConsole->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "demo_player", "error during intpack decompression");
				Stop();
				break;
			}
		}

		if(ChunkType == CHUNKTYPE_DELTA)
		{
			// process delta snapshot
			static char s_aNewsnap[CSnapshot::MAX_SIZE];

			GotSnapshot = 1;

			DataSize = m_pSnapshotDelta->UnpackDelta((CSnapshot *)m_aLastSnapshotData, (CSnapshot *)s_aNewsnap, s_aData, DataSize);

			if(DataSize >= 0)
			{
				if(m_pListener)
					m_pListener->OnDemoPlayerSnapshot(s_aNewsnap, DataSize);

				m_LastSnapshotDataSize = DataSize;
				mem_copy(m_aLastSnapshotData, s_aNewsnap, DataSize);
			}
			else
			{
				if(m_pConsole)
				{
					char aBuf[256];
					str_format(aBuf, sizeof(aBuf), "error during unpacking of delta, err=%d", DataSize);
					m_pConsole->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "demo_player", aBuf);
				}
			}
		}
		else if(ChunkType == CHUNKTYPE_SNAPSHOT)
		{
			// process full snapshot
			GotSnapshot = 1;

			m_LastSnapshotDataSize = DataSize;
			mem_copy(m_aLastSnapshotData, s_aData, DataSize);
			if(m_pListener)
				m_pListener->OnDemoPlayerSnapshot(s_aData, DataSize);
		}
		else
		{
			// if there were no snapshots in this tick, replay the last one
			if(!GotSnapshot && m_pListener && m_LastSnapshotDataSize != -1)
			{
				GotSnapshot = 1;
				m_pListener->OnDemoPlayerSnapshot(m_aLastSnapshotData, m_LastSnapshotDataSize);
			}

			// check the remaining types
			if(ChunkType & CHUNKTYPEFLAG_TICKMARKER)
			{
				m_Info.m_NextTick = ChunkTick;
				break;
			}
			else if(ChunkType == CHUNKTYPE_MESSAGE)
			{
				if(m_pListener)
					m_pListener->OnDemoPlayerMessage(s_aData, DataSize);
			}
		}
	}
}

void CDemoPlayer::Pause()
{
	m_Info.m_Info.m_Paused = 1;
#if defined(CONF_VIDEORECORDER)
	if(IVideo::Current() && g_Config.m_ClVideoPauseWithDemo)
		IVideo::Current()->Pause(true);
#endif
}

void CDemoPlayer::Unpause()
{
	if(m_Info.m_Info.m_Paused)
	{
		/*m_Info.start_tick = m_Info.current_tick;
		m_Info.start_time = time_get();*/
		m_Info.m_Info.m_Paused = 0;
	}
#if defined(CONF_VIDEORECORDER)
	if(IVideo::Current() && g_Config.m_ClVideoPauseWithDemo)
		IVideo::Current()->Pause(false);
#endif
}

int CDemoPlayer::Load(class IStorage *pStorage, class IConsole *pConsole, const char *pFilename, int StorageType)
{
	m_pConsole = pConsole;
	m_File = pStorage->OpenFile(pFilename, IOFLAG_READ, StorageType);
	if(!m_File)
	{
		if(m_pConsole)
		{
			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), "could not open '%s'", pFilename);
			m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "demo_player", aBuf);
		}
		return -1;
	}

	// store the filename
	str_copy(m_aFilename, pFilename, sizeof(m_aFilename));

	// clear the playback info
	mem_zero(&m_Info, sizeof(m_Info));
	m_Info.m_Info.m_FirstTick = -1;
	m_Info.m_Info.m_LastTick = -1;
	m_Info.m_NextTick = -1;
	m_Info.m_Info.m_CurrentTick = -1;
	m_Info.m_PreviousTick = -1;
	m_Info.m_Info.m_Speed = 1;
	m_SpeedIndex = 4;

	m_LastSnapshotDataSize = -1;

	// read the header
	io_read(m_File, &m_Info.m_Header, sizeof(m_Info.m_Header));
	if(mem_comp(m_Info.m_Header.m_aMarker, s_aHeaderMarker, sizeof(s_aHeaderMarker)) != 0)
	{
		if(m_pConsole)
		{
			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), "'%s' is not a demo file", pFilename);
			m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "demo_player", aBuf);
		}
		io_close(m_File);
		m_File = 0;
		return -1;
	}

	if(m_Info.m_Header.m_Version < s_OldVersion)
	{
		if(m_pConsole)
		{
			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), "demo version %d is not supported", m_Info.m_Header.m_Version);
			m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "demo_player", aBuf);
		}
		io_close(m_File);
		m_File = 0;
		return -1;
	}
	else if(m_Info.m_Header.m_Version > s_OldVersion)
		io_read(m_File, &m_Info.m_TimelineMarkers, sizeof(m_Info.m_TimelineMarkers));

	SHA256_DIGEST Sha256 = SHA256_ZEROED;
	if(m_Info.m_Header.m_Version >= s_Sha256Version)
	{
		CUuid ExtensionUuid = {};
		io_read(m_File, &ExtensionUuid.m_aData, sizeof(ExtensionUuid.m_aData));

		if(ExtensionUuid == SHA256_EXTENSION)
		{
			io_read(m_File, &Sha256, sizeof(SHA256_DIGEST)); // need a safe read
		}
		else
		{
			// This hopes whatever happened during the version increment didn't add something here
			dbg_msg("demo", "demo version incremented, but not by ddnet");
			io_seek(m_File, -(int)sizeof(ExtensionUuid.m_aData), IOSEEK_CUR);
		}
	}

	// get demo type
	if(!str_comp(m_Info.m_Header.m_aType, "client"))
		m_DemoType = DEMOTYPE_CLIENT;
	else if(!str_comp(m_Info.m_Header.m_aType, "server"))
		m_DemoType = DEMOTYPE_SERVER;
	else
		m_DemoType = DEMOTYPE_INVALID;

	// read map
	unsigned MapSize = (m_Info.m_Header.m_aMapSize[0] << 24) | (m_Info.m_Header.m_aMapSize[1] << 16) | (m_Info.m_Header.m_aMapSize[2] << 8) | (m_Info.m_Header.m_aMapSize[3]);

	// check if we already have the map
	// TODO: improve map checking (maps folder, check crc)
	unsigned Crc = (m_Info.m_Header.m_aMapCrc[0] << 24) | (m_Info.m_Header.m_aMapCrc[1] << 16) | (m_Info.m_Header.m_aMapCrc[2] << 8) | (m_Info.m_Header.m_aMapCrc[3]);

	// save byte offset of map for later use
	m_MapOffset = io_tell(m_File);
	io_skip(m_File, MapSize);

	// store map information
	m_MapInfo.m_Crc = Crc;
	m_MapInfo.m_Sha256 = Sha256;
	m_MapInfo.m_Size = MapSize;
	str_copy(m_MapInfo.m_aName, m_Info.m_Header.m_aMapName, sizeof(m_MapInfo.m_aName));

	if(m_Info.m_Header.m_Version > s_OldVersion)
	{
		// get timeline markers
		int Num = ((m_Info.m_TimelineMarkers.m_aNumTimelineMarkers[0] << 24) & 0xFF000000) | ((m_Info.m_TimelineMarkers.m_aNumTimelineMarkers[1] << 16) & 0xFF0000) |
			  ((m_Info.m_TimelineMarkers.m_aNumTimelineMarkers[2] << 8) & 0xFF00) | (m_Info.m_TimelineMarkers.m_aNumTimelineMarkers[3] & 0xFF);
		m_Info.m_Info.m_NumTimelineMarkers = minimum(Num, (int)MAX_TIMELINE_MARKERS);
		for(int i = 0; i < m_Info.m_Info.m_NumTimelineMarkers; i++)
		{
			char *pTimelineMarker = m_Info.m_TimelineMarkers.m_aTimelineMarkers[i];
			m_Info.m_Info.m_aTimelineMarkers[i] = ((pTimelineMarker[0] << 24) & 0xFF000000) | ((pTimelineMarker[1] << 16) & 0xFF0000) |
							      ((pTimelineMarker[2] << 8) & 0xFF00) | (pTimelineMarker[3] & 0xFF);
		}
	}

	// scan the file for interesting points
	ScanFile();

	// reset slice markers
	g_Config.m_ClDemoSliceBegin = -1;
	g_Config.m_ClDemoSliceEnd = -1;

	// ready for playback
	return 0;
}

bool CDemoPlayer::ExtractMap(class IStorage *pStorage)
{
	if(!m_MapInfo.m_Size)
		return false;

	long CurSeek = io_tell(m_File);

	// get map data
	io_seek(m_File, m_MapOffset, IOSEEK_START);
	unsigned char *pMapData = (unsigned char *)malloc(m_MapInfo.m_Size);
	io_read(m_File, pMapData, m_MapInfo.m_Size);
	io_seek(m_File, CurSeek, IOSEEK_START);

	// handle sha256
	SHA256_DIGEST Sha256 = SHA256_ZEROED;
	if(m_Info.m_Header.m_Version >= s_Sha256Version)
		Sha256 = m_MapInfo.m_Sha256;
	else
	{
		Sha256 = sha256(pMapData, m_MapInfo.m_Size);
		m_MapInfo.m_Sha256 = Sha256;
	}

	// construct name
	char aSha[SHA256_MAXSTRSIZE], aMapFilename[128];
	sha256_str(Sha256, aSha, sizeof(aSha));
	str_format(aMapFilename, sizeof(aMapFilename), "downloadedmaps/%s_%s.map", m_Info.m_Header.m_aMapName, aSha);

	// save map
	IOHANDLE MapFile = pStorage->OpenFile(aMapFilename, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(!MapFile)
		return false;

	io_write(MapFile, pMapData, m_MapInfo.m_Size);
	io_close(MapFile);

	// free data
	free(pMapData);

	return true;
}

int CDemoPlayer::NextFrame()
{
	DoTick();
	return IsPlaying();
}

int64_t CDemoPlayer::time()
{
#if defined(CONF_VIDEORECORDER)
	static bool s_Recording = false;
	if(IVideo::Current())
	{
		if(!s_Recording)
		{
			s_Recording = true;
			m_Info.m_LastUpdate = IVideo::Time();
		}
		return IVideo::Time();
	}
	else
	{
		int64_t Now = time_get();
		if(s_Recording)
		{
			s_Recording = false;
			m_Info.m_LastUpdate = Now;
		}
		return Now;
	}
#else
	return time_get();
#endif
}

int CDemoPlayer::Play()
{
	// fill in previous and next tick
	while(m_Info.m_PreviousTick == -1 && IsPlaying())
		DoTick();

	// set start info
	/*m_Info.start_tick = m_Info.previous_tick;
	m_Info.start_time = time_get();*/
	m_Info.m_CurrentTime = m_Info.m_PreviousTick * time_freq() / SERVER_TICK_SPEED;
	m_Info.m_LastUpdate = time();
	return 0;
}

int CDemoPlayer::SeekPercent(float Percent)
{
	int WantedTick = m_Info.m_Info.m_FirstTick + ((m_Info.m_Info.m_LastTick - m_Info.m_Info.m_FirstTick) * Percent);
	return SetPos(WantedTick);
}

int CDemoPlayer::SeekTime(float Seconds)
{
	int WantedTick = m_Info.m_Info.m_CurrentTick + (Seconds * SERVER_TICK_SPEED);
	return SetPos(WantedTick);
}

int CDemoPlayer::SetPos(int WantedTick)
{
	if(!m_File)
		return -1;

	// -5 because we have to have a current tick and previous tick when we do the playback
	WantedTick = clamp(WantedTick, m_Info.m_Info.m_FirstTick, m_Info.m_Info.m_LastTick) - 5;

	// get correct key frame
	int KeyFrame = 0;
	while(KeyFrame < m_Info.m_SeekablePoints - 1 && m_pKeyFrames[KeyFrame].m_Tick < WantedTick)
	{
		KeyFrame++;
	}
	while(KeyFrame > 0 && m_pKeyFrames[KeyFrame].m_Tick > WantedTick)
	{
		KeyFrame--;
	}

	// seek to the correct key frame
	io_seek(m_File, m_pKeyFrames[KeyFrame].m_Filepos, IOSEEK_START);

	m_Info.m_NextTick = -1;
	m_Info.m_Info.m_CurrentTick = -1;
	m_Info.m_PreviousTick = -1;

	// playback everything until we hit our tick
	while(m_Info.m_PreviousTick < WantedTick && IsPlaying())
		DoTick();

	Play();

	return 0;
}

void CDemoPlayer::SetSpeed(float Speed)
{
	m_Info.m_Info.m_Speed = clamp(Speed, 0.f, 256.f);
}

void CDemoPlayer::SetSpeedIndex(int Offset)
{
	m_SpeedIndex = clamp(m_SpeedIndex + Offset, 0, (int)(sizeof(g_aSpeeds) / sizeof(g_aSpeeds[0]) - 1));
	SetSpeed(g_aSpeeds[m_SpeedIndex]);
}

int CDemoPlayer::Update(bool RealTime)
{
	int64_t Now = time();
	int64_t Deltatime = Now - m_Info.m_LastUpdate;
	m_Info.m_LastUpdate = Now;

	if(!IsPlaying())
		return 0;

	if(m_Info.m_Info.m_Paused)
	{
	}
	else
	{
		int64_t Freq = time_freq();
		m_Info.m_CurrentTime += (int64_t)(Deltatime * (double)m_Info.m_Info.m_Speed);

		while(1)
		{
			int64_t CurtickStart = (m_Info.m_Info.m_CurrentTick) * Freq / SERVER_TICK_SPEED;

			// break if we are ready
			if(RealTime && CurtickStart > m_Info.m_CurrentTime)
				break;

			// do one more tick
			DoTick();

			if(m_Info.m_Info.m_Paused)
				return 0;
		}

		// update intratick
		{
			int64_t CurtickStart = (m_Info.m_Info.m_CurrentTick) * Freq / SERVER_TICK_SPEED;
			int64_t PrevtickStart = (m_Info.m_PreviousTick) * Freq / SERVER_TICK_SPEED;
			m_Info.m_IntraTick = (m_Info.m_CurrentTime - PrevtickStart) / (float)(CurtickStart - PrevtickStart);
			m_Info.m_IntraTickSincePrev = (m_Info.m_CurrentTime - PrevtickStart) / (float)(Freq / SERVER_TICK_SPEED);
			m_Info.m_TickTime = (m_Info.m_CurrentTime - PrevtickStart) / (float)Freq;
			if(m_UpdateIntraTimesFunc)
				m_UpdateIntraTimesFunc();
		}

		if(m_Info.m_Info.m_CurrentTick == m_Info.m_PreviousTick ||
			m_Info.m_Info.m_CurrentTick == m_Info.m_NextTick)
		{
			if(m_pConsole)
			{
				char aBuf[256];
				str_format(aBuf, sizeof(aBuf), "tick error prev=%d cur=%d next=%d",
					m_Info.m_PreviousTick, m_Info.m_Info.m_CurrentTick, m_Info.m_NextTick);
				m_pConsole->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "demo_player", aBuf);
			}
		}
		m_Time += m_TickTime;
	}
	return 0;
}

int CDemoPlayer::Stop()
{
#if defined(CONF_VIDEORECORDER)
	if(IVideo::Current())
		IVideo::Current()->Stop();
#endif

	if(!m_File)
		return -1;

	if(m_pConsole)
		m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "demo_player", "Stopped playback");
	io_close(m_File);
	m_File = 0;
	free(m_pKeyFrames);
	m_pKeyFrames = 0;
	str_copy(m_aFilename, "", sizeof(m_aFilename));
	return 0;
}

void CDemoPlayer::GetDemoName(char *pBuffer, int BufferSize) const
{
	const char *pFileName = m_aFilename;
	const char *pExtractedName = pFileName;
	const char *pEnd = 0;
	for(; *pFileName; ++pFileName)
	{
		if(*pFileName == '/' || *pFileName == '\\')
			pExtractedName = pFileName + 1;
		else if(*pFileName == '.')
			pEnd = pFileName;
	}

	int Length = pEnd > pExtractedName ? minimum(BufferSize, (int)(pEnd - pExtractedName + 1)) : BufferSize;
	str_copy(pBuffer, pExtractedName, Length);
}

bool CDemoPlayer::GetDemoInfo(class IStorage *pStorage, const char *pFilename, int StorageType, CDemoHeader *pDemoHeader, CTimelineMarkers *pTimelineMarkers, CMapInfo *pMapInfo) const
{
	if(!pDemoHeader || !pTimelineMarkers || !pMapInfo)
		return false;

	mem_zero(pDemoHeader, sizeof(CDemoHeader));
	mem_zero(pTimelineMarkers, sizeof(CTimelineMarkers));

	IOHANDLE File = pStorage->OpenFile(pFilename, IOFLAG_READ, StorageType);
	if(!File)
		return false;

	io_read(File, pDemoHeader, sizeof(CDemoHeader));
	io_read(File, pTimelineMarkers, sizeof(CTimelineMarkers));

	str_copy(pMapInfo->m_aName, pDemoHeader->m_aMapName, sizeof(pMapInfo->m_aName));
	pMapInfo->m_Crc = (pDemoHeader->m_aMapCrc[0] << 24) | (pDemoHeader->m_aMapCrc[1] << 16) | (pDemoHeader->m_aMapCrc[2] << 8) | (pDemoHeader->m_aMapCrc[3]);

	SHA256_DIGEST Sha256 = SHA256_ZEROED;
	if(pDemoHeader->m_Version >= s_Sha256Version)
	{
		CUuid ExtensionUuid = {};
		io_read(File, &ExtensionUuid.m_aData, sizeof(ExtensionUuid.m_aData));

		if(ExtensionUuid == SHA256_EXTENSION)
		{
			io_read(File, &Sha256, sizeof(SHA256_DIGEST)); // need a safe read
		}
		else
		{
			// This hopes whatever happened during the version increment didn't add something here
			dbg_msg("demo", "demo version incremented, but not by ddnet");
			io_seek(File, -(int)sizeof(ExtensionUuid.m_aData), IOSEEK_CUR);
		}
	}
	pMapInfo->m_Sha256 = Sha256;

	pMapInfo->m_Size = (pDemoHeader->m_aMapSize[0] << 24) | (pDemoHeader->m_aMapSize[1] << 16) | (pDemoHeader->m_aMapSize[2] << 8) | (pDemoHeader->m_aMapSize[3]);

	io_close(File);
	return !(mem_comp(pDemoHeader->m_aMarker, s_aHeaderMarker, sizeof(s_aHeaderMarker)) || pDemoHeader->m_Version < s_OldVersion);
}

int CDemoPlayer::GetDemoType() const
{
	if(m_File)
		return m_DemoType;
	return DEMOTYPE_INVALID;
}

void CDemoEditor::Init(const char *pNetVersion, class CSnapshotDelta *pSnapshotDelta, class IConsole *pConsole, class IStorage *pStorage)
{
	m_pNetVersion = pNetVersion;
	m_pSnapshotDelta = pSnapshotDelta;
	m_pConsole = pConsole;
	m_pStorage = pStorage;
}

void CDemoEditor::Slice(const char *pDemo, const char *pDst, int StartTick, int EndTick, DEMOFUNC_FILTER pfnFilter, void *pUser)
{
	class CDemoPlayer DemoPlayer(m_pSnapshotDelta);
	class CDemoRecorder DemoRecorder(m_pSnapshotDelta);

	m_pDemoPlayer = &DemoPlayer;
	m_pDemoRecorder = &DemoRecorder;

	m_pDemoPlayer->SetListener(this);

	m_SliceFrom = StartTick;
	m_SliceTo = EndTick;
	m_Stop = false;

	if(m_pDemoPlayer->Load(m_pStorage, m_pConsole, pDemo, IStorage::TYPE_ALL) == -1)
		return;

	const CMapInfo *pMapInfo = m_pDemoPlayer->GetMapInfo();
	const CDemoPlayer::CPlaybackInfo *pInfo = m_pDemoPlayer->Info();

	SHA256_DIGEST Sha256 = pMapInfo->m_Sha256;
	if(pInfo->m_Header.m_Version < s_Sha256Version)
	{
		if(m_pDemoPlayer->ExtractMap(m_pStorage))
			Sha256 = pMapInfo->m_Sha256;
	}

	if(m_pDemoRecorder->Start(m_pStorage, m_pConsole, pDst, m_pNetVersion, pMapInfo->m_aName, &Sha256, pMapInfo->m_Crc, "client", pMapInfo->m_Size, NULL, NULL, pfnFilter, pUser) == -1)
		return;

	m_pDemoPlayer->Play();

	while(m_pDemoPlayer->IsPlaying() && !m_Stop)
	{
		m_pDemoPlayer->Update(false);

		if(pInfo->m_Info.m_Paused)
			break;
	}

	m_pDemoPlayer->Stop();
	m_pDemoRecorder->Stop();
} // NOLINT(clang-analyzer-unix.Malloc)

void CDemoEditor::OnDemoPlayerSnapshot(void *pData, int Size)
{
	const CDemoPlayer::CPlaybackInfo *pInfo = m_pDemoPlayer->Info();

	if(m_SliceTo != -1 && pInfo->m_Info.m_CurrentTick > m_SliceTo)
		m_Stop = true;
	else if(m_SliceFrom == -1 || pInfo->m_Info.m_CurrentTick >= m_SliceFrom)
		m_pDemoRecorder->RecordSnapshot(pInfo->m_Info.m_CurrentTick, pData, Size);
}

void CDemoEditor::OnDemoPlayerMessage(void *pData, int Size)
{
	const CDemoPlayer::CPlaybackInfo *pInfo = m_pDemoPlayer->Info();

	if(m_SliceTo != -1 && pInfo->m_Info.m_CurrentTick > m_SliceTo)
		m_Stop = true;
	else if(m_SliceFrom == -1 || pInfo->m_Info.m_CurrentTick >= m_SliceFrom)
		m_pDemoRecorder->RecordMessage(pData, Size);
}
