#include "stdafx.h"
#include "ipfw.h"
#include "ipfw_int.h"

/*-----------------------------------------------------------------------------
-----------------------------------------------------------------------------*/
CIpfw::CIpfw(void):
	hDriver(INVALID_HANDLE_VALUE)
{
	hDriver = CreateFile (_T("\\\\.\\Ipfw"), GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if( hDriver != INVALID_HANDLE_VALUE )
	{
		OutputDebugString(_T("Connected to IPFW"));
	}
	else
	{
		OutputDebugString(_T("Could not connect to IPFW"));
	}
}

/*-----------------------------------------------------------------------------
-----------------------------------------------------------------------------*/
CIpfw::~CIpfw(void)
{
	if( hDriver != INVALID_HANDLE_VALUE )
		CloseHandle(hDriver);
}

void DumpBuff(const unsigned char * buff, unsigned long len)
{
	CString out, tmp;
	if( buff && len )
	{
		int count = 0;
		while( len )
		{
			unsigned char cval = *buff;
			unsigned int val = cval;
			if( !count )
				out += _T("\n    ");
			tmp.Format(_T("%02X "),val);
			out += tmp;

			count++;
			buff++;
			if( count >= 8 )
				count = 0;
			len--;
		}
	}

	OutputDebugString(out);
}

/*-----------------------------------------------------------------------------
-----------------------------------------------------------------------------*/
bool CIpfw::Set(int cmd, void * data, size_t len)
{
	bool ret = false;

	if( hDriver != INVALID_HANDLE_VALUE )
	{
		// copy the data to the structure to send down to the driver
		size_t size = sizeof(struct sockopt) + len;
		struct sockopt * s = (struct sockopt *)malloc(size);
		if( s )
		{
			s->sopt_dir = SOPT_SET;
			s->sopt_name = cmd;
			s->sopt_valsize = len;
			s->sopt_val = (void *)(s+1);

			memcpy(s->sopt_val, data, len);

//			CString buff;
//			buff.Format(_T("IP_FW_SETSOCKOPT - %d bytes:"), len);
//			OutputDebugString(buff);
//			DumpBuff((const unsigned char *)data, len);

			DWORD n;
			if( DeviceIoControl(hDriver, IP_FW_SETSOCKOPT, s, size, s, size, &n, NULL) )
				ret = true;

			free(s);
		}
	}

	return ret;
}

/*-----------------------------------------------------------------------------
-----------------------------------------------------------------------------*/
bool CIpfw::Get(int cmd, void * data, size_t &len)
{
	bool ret = false;

	if( hDriver != INVALID_HANDLE_VALUE )
	{
		// copy the data to the structure to send down to the driver
		size_t size = sizeof(struct sockopt) + len;
		struct sockopt * s = (struct sockopt *)malloc(size);
		if( s )
		{
			s->sopt_dir = SOPT_GET;
			s->sopt_name = cmd;
			s->sopt_valsize = len;
			s->sopt_val = (void *)(s+1);

			memcpy(s->sopt_val, data, len);

//			CString buff;
//			buff.Format(_T("IP_FW_GETSOCKOPT - %d bytes:"), len);
//			OutputDebugString(buff);
//			DumpBuff((const unsigned char *)data, len);

			DWORD n;
			if( DeviceIoControl(hDriver, IP_FW_GETSOCKOPT, s, size, s, size, &n, NULL) )
			{
				// copy the results back
				if( s->sopt_valsize <= len )
				{
					len = s->sopt_valsize;
					if( len > 0 )
						memcpy(data, s->sopt_val, len);
					ret = true;
				}
			}

			free(s);
		}
	}

	return ret;
}

/*-----------------------------------------------------------------------------
-----------------------------------------------------------------------------*/
bool CIpfw::Flush()
{
	bool ret = false;

	if( hDriver != INVALID_HANDLE_VALUE )
	{
		// Flush both dummynet and IPFW
		ret = Set(IP_FW_FLUSH, NULL, 0);

		if( ret )
			OutputDebugString(_T("IPFW flushed"));
	}

	return ret;
}

/*-----------------------------------------------------------------------------
-----------------------------------------------------------------------------*/
bool CIpfw::CreatePipe(unsigned int num, unsigned long bandwidth, unsigned long delay, double plr)
{
	bool ret = false;

	if( hDriver != INVALID_HANDLE_VALUE )
	{
		#pragma pack(push)
		#pragma pack(1)
		struct {
			struct dn_id	header;
			struct dn_sch	sch;
			struct dn_link	link;
			struct dn_fs	fs;
		} cmd;
		#pragma pack(pop)

		memset(&cmd, 0, sizeof(cmd));

		cmd.header.len = sizeof(cmd.header);
		cmd.header.type = DN_CMD_CONFIG;
		cmd.header.id = DN_API_VERSION;

		// scheduler
		cmd.sch.oid.len = sizeof(cmd.sch);
		cmd.sch.oid.type = DN_SCH;
		cmd.sch.sched_nr = num;
		cmd.sch.oid.subtype = 0;	/* defaults to WF2Q+ */
		cmd.sch.flags = DN_PIPE_CMD;

		// link
		cmd.link.oid.len = sizeof(cmd.link);
		cmd.link.oid.type = DN_LINK;
		cmd.link.link_nr = num;
		cmd.link.bandwidth = bandwidth;
		cmd.link.delay = delay;

		// flowset
		cmd.fs.oid.len = sizeof(cmd.fs);
		cmd.fs.oid.type = DN_FS;
		cmd.fs.fs_nr = num + 2*DN_MAX_ID;
		cmd.fs.sched_nr = num + DN_MAX_ID;
		for(int j = 0; j < _countof(cmd.fs.par); j++)
			cmd.fs.par[j] = -1;
		if( plr > 0 && plr <= 1.0 )
			cmd.fs.plr = (int)(plr*0x7fffffff);

		ret = Set(IP_DUMMYNET3, &cmd, sizeof(cmd));

		if( ret )
			OutputDebugString(_T("Pipe created"));
	}

	return ret;
}

/*-----------------------------------------------------------------------------
-----------------------------------------------------------------------------*/
bool CIpfw::DeletePipe(unsigned int num)
{
	bool ret = false;

	if( hDriver != INVALID_HANDLE_VALUE )
	{
		#pragma pack(push)
		#pragma pack(1)
		struct {
			struct dn_id oid;
			uintptr_t a[1];	/* add more if we want a list */
		} cmd;
		#pragma pack(pop)

		cmd.oid.len = sizeof(cmd);
		cmd.oid.type = DN_CMD_DELETE;
		cmd.oid.subtype = DN_LINK;
		cmd.oid.id = DN_API_VERSION;

		cmd.a[0] = num;

		ret = Set(IP_DUMMYNET3, &cmd, sizeof(cmd));

		if( ret )
			OutputDebugString(_T("Pipe deleted"));
	}

	return ret;
}

/*-----------------------------------------------------------------------------
	Add traffic to or from the given port to the giveen pipe (depending if we are 
	doing inbound or outbound)
-----------------------------------------------------------------------------*/
unsigned int CIpfw::AddPort(unsigned int pipeNum, unsigned short port, bool in)
{
	unsigned int ret = 0;

	if( hDriver != INVALID_HANDLE_VALUE )
	{
	}

	return ret;
}

/*-----------------------------------------------------------------------------
	Delete the given IPFW rule
-----------------------------------------------------------------------------*/
bool CIpfw::Delete(unsigned int rule)
{
	bool ret = false;

	if( hDriver != INVALID_HANDLE_VALUE )
	{
	}

	return ret;
}
