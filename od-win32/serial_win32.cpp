/*
* UAE - The Un*x Amiga Emulator
*
*  Serial Line Emulation
*
* (c) 1996, 1997 Stefan Reinauer <stepan@linux.de>
* (c) 1997 Christian Schmitt <schmitt@freiburg.linux.de>
*
*/

#include "sysconfig.h"
#ifdef SERIAL_ENET
#include "enet/enet.h"
#endif
#include "sysdeps.h"

#include "options.h"
#include "serial.h"
#include "serial_host.h"
#include "parser.h"

#define SERMAP_SIZE 256
struct sermap_buffer
{
	volatile ULONG version;
	volatile uae_u32 active_read;
	volatile uae_u32 active_write;
	volatile uae_u32 read_offset;
	volatile uae_u32 write_offset;
	volatile uae_u32 data[SERMAP_SIZE];
};
static struct sermap_buffer *sermap1, *sermap2;
static HANDLE sermap_handle;
static uae_u8 *sermap_data;
static bool sermap_master;
#define SER_MEMORY_MAPPING _T("WinUAE_Serial")

void host_serial_map_send(uae_u32 data)
{
	uae_u32 v;

	sermap1->active_write = true;
	if (!sermap1->active_read) {
		return;
	}
	v = sermap1->write_offset;
	if (((v + 1) & (SERMAP_SIZE - 1)) == sermap1->read_offset) {
		write_log(_T("Shared serial port memory overflow!\n"));
		return;
	}
	sermap1->data[v] = data;
	v++;
	v &= (SERMAP_SIZE - 1);
	sermap1->write_offset = v;
}
uae_u32 host_serial_map_receive(void)
{
	uae_u32 v;
	uae_u32 data;
	sermap2->active_read = true;
	if (!sermap2->active_write) {
		return 0xffffffff;
	}
	v = sermap2->read_offset;
	if (v == sermap2->write_offset) {
		return 0xffffffff;
	}
	data = sermap2->data[v];
	v++;
	v &= (SERMAP_SIZE - 1);
	sermap2->read_offset = v;
	return data;
}

void host_serial_map_deactivate(void)
{
	if (sermap1) {
		sermap1->active_write = 0;
		sermap1->write_offset = sermap1->read_offset;
	}
	if (sermap2) {
		sermap2->active_read = 0;
		sermap2->read_offset = sermap2->write_offset;
	}
}

int shmem_serial_state(void)
{
	if (!sermap_handle)
		return 0;
	if (sermap_master)
		return 1;
	return 2;
}

void shmem_serial_delete(void)
{
	serial_map_deactivate();
	sermap_master = false;
	if (sermap_data)
		UnmapViewOfFile(sermap_data);
	if (sermap_handle)
		CloseHandle(sermap_handle);
	sermap_data = NULL;
	sermap_handle = NULL;
	sermap1 = sermap2 = NULL;
}

bool shmem_serial_create(void)
{
	shmem_serial_delete();
	sermap_handle = OpenFileMapping(FILE_MAP_ALL_ACCESS, FALSE, SER_MEMORY_MAPPING);
	if (!sermap_handle) {
		sermap_handle = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(struct sermap_buffer) * 2, SER_MEMORY_MAPPING);
		if (!sermap_handle) {
			write_log(_T("Failed to create shared serial port memory: %d\n"), GetLastError());
			return false;
		}
		sermap_master = true;
		write_log(_T("Created internal serial port shared memory\n"));
	} else {
		write_log(_T("Found already existing serial port shared memory\n"));
	}
	sermap_data = (uae_u8*)MapViewOfFile(sermap_handle, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(struct sermap_buffer) * 2);
	if (!sermap_data) {
		write_log(_T("Shared serial port memory MapViewOfFile() failed: %d\n"), GetLastError());
		return false;
	}
	if (sermap_master) {
		sermap1 = (struct sermap_buffer*)sermap_data;
		sermap2 = (struct sermap_buffer*)(sermap_data + sizeof(struct sermap_buffer));
		sermap1->version = version;
		sermap2->version = version;
	} else {
		sermap2 = (struct sermap_buffer*)sermap_data;
		sermap1 = (struct sermap_buffer*)(sermap_data + sizeof(struct sermap_buffer));
		if (sermap2->version != version || sermap1->version != version) {
			write_log(_T("Shared serial port memory version mismatch %08x != %08x\n"), sermap1->version, version);
			shmem_serial_delete();
			return false;
		}
	}
	return true;
}

#ifdef SERIAL_ENET
extern int serial_enet;
static ENetHost *enethost, *enetclient;
static ENetPeer *enetpeer, *enet;
static int enetmode;
static uae_u16 enet_receive[256];
static int enet_receive_off_w, enet_receive_off_r;

static void enet_service (int serveronly)
{
	ENetEvent evt;
	ENetAddress address;
	int got;
	
	if (enetmode == 0)
		return;

	got = 1;
	while (got) {
		got = 0;
		if (enetmode > 0) {
			while (enet_host_service (enethost, &evt, 0)) {
				got = 1;
				switch (evt.type)
				{
					case ENET_EVENT_TYPE_CONNECT:
						address = evt.peer->address;
						write_log (_T("ENET_SERVER: connect from %d.%d.%d.%d:%u\n"),
							(address.host >> 0) & 0xff, (address.host >> 8) & 0xff, (address.host >> 16) & 0xff, (address.host >> 24) & 0xff,
							address.port);
						evt.peer->data = 0;
					break;
					case ENET_EVENT_TYPE_RECEIVE:
					{
						uae_u8 *p = evt.packet->data;
						size_t len = evt.packet->dataLength;
						if (len == 6 && !memcmp (p, "UAE_", 4)) {
							if (((enet_receive_off_w + 1) & 0xff) != enet_receive_off_r) {
								enet_receive[enet_receive_off_w++] = (p[4] << 8) | p[5];
							}
						}

						enet_packet_destroy (evt.packet);
					}
					break;
					case ENET_EVENT_TYPE_DISCONNECT:
						address = evt.peer->address;
						write_log (_T("ENET_SERVER: disconnect from %d.%d.%d.%d:%u\n"),
							(address.host >> 0) & 0xff, (address.host >> 8) & 0xff, (address.host >> 16) & 0xff, (address.host >> 24) & 0xff,
							address.port);
					break;
				}
			}
		}
		if (!serveronly) {
			while (enet_host_service (enetclient, &evt, 0)) {
				got = 1;
				switch (evt.type)
				{
					default:
					case 0:
					write_log (_T("ENET_CLIENT: %d\n"), evt.type);
					break;
				}
			}
		}
	}
}

static void enet_disconnect (ENetPeer *peer)
{
	ENetEvent evt;
	int cnt = 30;

	if (!peer)
		return;

	write_log (_T("ENET_CLIENT: disconnecting..\n"));
	enet_peer_disconnect (peer, 0);
	while (cnt-- > 0) {
		enet_service (1);
		while (enet_host_service (enetclient, &evt, 100) > 0)
		{
			switch (evt.type)
			{
			case ENET_EVENT_TYPE_RECEIVE:
				enet_packet_destroy (evt.packet);
				break;

			case ENET_EVENT_TYPE_DISCONNECT:
				write_log (_T("ENET_CLIENT: disconnection succeeded\n"));
				enetpeer = NULL;
				return;
			}
		}
	}
	write_log (_T("ENET_CLIENT: disconnection forced\n"));
	enet_peer_reset (enetpeer);
	enetpeer = NULL;
}

void enet_close (void)
{
	enet_disconnect (enetpeer);
	if (enetclient)
		enet_host_destroy (enetclient);
	enetclient = NULL;
	if (enethost)
		enet_host_destroy (enethost);
	enethost = NULL;
	serial_enet = 0;
	enetmode = 0;
}

int enet_open (TCHAR *name)
{
	ENetAddress address;
	ENetPacket *p;
	static int initialized;
	uae_u8 data[16];
	int cnt;

	if (!initialized) {
		int err = enet_initialize ();
		if (err) {
			write_log (_T("ENET: initialization failed: %d\n"), err);
			return 0;
		}
		initialized = 1;
	}
	
	enet_close ();
	enetmode = 0;
	if (!_tcsnicmp (name, _T("ENET:H"), 6)) {
		address.host = ENET_HOST_ANY;
		address.port = 1234;
		enethost = enet_host_create (&address, 2, 0, 0);
		if (enethost == NULL) {
			write_log (_T("ENET_SERVER: enet_host_create(server) failed\n"));
			enet_close ();
			return 0;
		}
		write_log (_T("ENET_SERVER: server created\n"));
		enetmode = 1;
	} else {
		enetmode = -1;
	}
	enetclient = enet_host_create (NULL, 1, 0, 0);
	if (enetclient == NULL) {
		write_log (_T("ENET_CLIENT: enet_host_create(client) failed\n"));
		enet_close ();
		return 0;
	}
	write_log (_T("ENET_CLIENT: client created\n"));
	enet_address_set_host (&address, enetmode > 0 ? "127.0.0.1" : "192.168.0.10");
	address.port = 1234;
	enetpeer = enet_host_connect (enetclient, &address, 2);
	if (enetpeer == NULL) {
		write_log (_T("ENET_CLIENT: connection to host %d.%d.%d.%d:%d failed\n"),
			(address.host >> 0) & 0xff, (address.host >> 8) & 0xff, (address.host >> 16) & 0xff, (address.host >> 24) & 0xff, address.port);
		enet_host_destroy (enetclient);
		enetclient = NULL;
	}
	write_log (_T("ENET_CLIENT: connecting to %d.%d.%d.%d:%d...\n"),
		(address.host >> 0) & 0xff, (address.host >> 8) & 0xff, (address.host >> 16) & 0xff, (address.host >> 24) & 0xff, address.port);
	cnt = 10 * 5;
	while (cnt-- > 0) {
		ENetEvent evt;
		enet_service (0);
		if (enet_host_service (enetclient, &evt, 100) > 0) {
			if (evt.type == ENET_EVENT_TYPE_CONNECT)
				break;
		}
	}
	if (cnt <= 0) {
		write_log (_T("ENET_CLIENT: connection failed, no response in 5 seconds\n"));
		enet_close ();
		return 0;
	}
	memcpy (data, "UAE_HELLO", 10);
	p = enet_packet_create (data, sizeof data, ENET_PACKET_FLAG_RELIABLE);
	enet_peer_send (enetpeer, 0, p);
	enet_host_flush (enetclient);
	write_log (_T("ENET: connected\n"));
	serial_enet = 1;
	return 1;
}

void enet_writeser (uae_u16 w)
{
	ENetPacket *p;
	uae_u8 data[16];

	memcpy (data, "UAE_", 4);
	data[4] = w >> 8;
	data[5] = w >> 0;
	write_log (_T("W=%04X "), w);
	p = enet_packet_create (data, 6, ENET_PACKET_FLAG_RELIABLE);
	enet_peer_send (enetpeer, 0, p);
	enet_host_flush (enetclient);
}

int enet_readseravail (void)
{
	enet_service (0);
	return enet_receive_off_r != enet_receive_off_w;
}

int enet_readser (uae_u16 *data)
{
	if (enet_receive_off_r == enet_receive_off_w)
		return 0;
	*data = enet_receive[enet_receive_off_r++];
	write_log (_T("R=%04X "), *data);
	enet_receive_off_r &= 0xff;
	return 1;
}
#endif

bool host_serial_map_available(void)
{
	return sermap_data != NULL;
}

void host_serial_hsync(void)
{
#ifdef AHI
	hsyncstuff();
#endif
}
