#ifndef WINUAE_OD_UNIX_UAESERIAL_UNIX_H
#define WINUAE_OD_UNIX_UAESERIAL_UNIX_H

#define UNIX_UAESERIAL_MAX_UNITS 32

const TCHAR *unix_uaeserial_get_port(int unit);
void unix_uaeserial_set_port(int unit, const TCHAR *port);

#endif /* WINUAE_OD_UNIX_UAESERIAL_UNIX_H */
