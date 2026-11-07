#ifndef UAE_SERIAL_HOST_H
#define UAE_SERIAL_HOST_H

/* Host-independent modem signals. Native backends translate their flags. */
enum {
	SERIAL_HOST_CAR = 1,
	SERIAL_HOST_DSR = 2,
	SERIAL_HOST_RI = 4,
	SERIAL_HOST_DTR = 8,
	SERIAL_HOST_RTS = 16,
	SERIAL_HOST_CTS = 32
};

#define SERIAL_INTERNAL _T("INTERNAL_SERIAL")
#define SERIAL_LOOPBACK _T("LOOPBACK_SERIAL")

int setbaud(int baud, int origbaud);
void getserstat(int *status);
void setserstat(int mask, int onoff);
int readser(int *buffer);
int readseravail(bool *breakcond);
void flushser(void);
void writeser(int c);
void writeser_flush(void);
int openser(const TCHAR *name);
void closeser(void);
int checkserwrite(int spaceneeded);
void serialuartbreak(int enabled);

void serial_map_deactivate(void);

bool host_serial_map_available(void);
void host_serial_map_send(uae_u32 data);
uae_u32 host_serial_map_receive(void);
void host_serial_map_deactivate(void);

#endif
